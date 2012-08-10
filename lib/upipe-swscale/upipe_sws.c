/*****************************************************************************
 * upipe_sws.c: upipe swscale (ffmpeg) module
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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
 *****************************************************************************/

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_linear_ubuf_mgr.h>
#include <upipe/upipe_helper_linear_output.h>
#include <upipe-swscale/upipe_sws.h>

#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h> // debug

#define ALIVE() { printf("# ALIVE: %s %s - %d\n", __FILE__, __func__, __LINE__); } // FIXME - debug - remove this

/** Picture size*/

struct picsize {
    size_t hsize;
    size_t vsize;
};

/** upipe_sws structure with swscale parameters */ 
struct upipe_sws {
    /** input flow */
    struct uref *input_flow;
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;

    /** output pipe */
    struct upipe *output;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** swscale image conversion context */
    struct SwsContext *convert_ctx;

    /** input image size */
    struct picsize srcsize;
    /** output image size */
    struct picsize dstsize;

    /** input image swscale format */
    enum PixelFormat srcformat;
    /** output image swscale format */
    enum PixelFormat dstformat;

    /** true if we have thrown the ready event */
    bool ready;

    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_sws, upipe);
UPIPE_HELPER_UREF_MGR(upipe_sws, uref_mgr);
UPIPE_HELPER_LINEAR_OUTPUT(upipe_sws, output, output_flow, output_flow_sent, uref_mgr);
UPIPE_HELPER_LINEAR_UBUF_MGR(upipe_sws, ubuf_mgr);

/** @internal @This allocates a swscale pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_sws_alloc(struct upipe_mgr *mgr)
{
    struct upipe_sws *upipe_sws = malloc(sizeof(struct upipe_sws));
    if (unlikely(upipe_sws == NULL)) return NULL;
    struct upipe *upipe = upipe_sws_to_upipe(upipe_sws);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_SWS_SIGNATURE;
    upipe_sws_init_uref_mgr(upipe);
    upipe_sws_init_ubuf_mgr(upipe);
    upipe_sws_init_output(upipe);
    upipe_sws->input_flow = NULL;
    upipe_sws->convert_ctx = NULL;
    memset(&upipe_sws->srcsize, 0, sizeof(struct picsize));
    memset(&upipe_sws->dstsize, 0, sizeof(struct picsize));

    upipe_sws->ready = false;
    return upipe;
}

/** @internal @This fetches chroma from uref/using defined picflow
 *  
 * @param uref uref structure
 * @param picflow uref structure describing the picture flow
 * @param str name of the chroma
 * @param strides strides array
 * @param slices array of pointers to data plans
 * @param idx index of the chroma in slices[]/strides[]
 */
static void inline upipe_sws_fetch_chroma(struct uref *uref, struct uref *picflow, const char *str, int *strides, uint8_t **slices, size_t idx)
{
    size_t stride = 0;
    slices[idx] = uref_pic_chroma(uref, picflow, str, &stride);
    strides[idx] = (int) stride;
}

static void upipe_sws_filldata(struct uref *uref, struct uref *picflow, int *strides, uint8_t **slices)
{
    // FIXME - hardcoded chroma fetch
    upipe_sws_fetch_chroma(uref, picflow, "y8", strides, slices, 0);
    upipe_sws_fetch_chroma(uref, picflow, "u8", strides, slices, 1);
    upipe_sws_fetch_chroma(uref, picflow, "v8", strides, slices, 2);
}

/** @internal @This configures swscale context
 *
 * @param upipe_sws upipe_sws structure
 * @param flow uref to the new flow
 * @param src pointer to new input picture size (can be NULL)
 * @param dst pointer to new output picture size (can be NULL)
 */

static bool upipe_sws_set_context(struct upipe *upipe, struct uref *flow, struct picsize *src, struct picsize *dst) // FIXME - refactor this
{
    assert(upipe);
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    struct picsize *srcsize = &upipe_sws->srcsize;
    struct picsize *dstsize = &upipe_sws->dstsize;

    if (flow) {
        ulog_warning(upipe->ulog, "sws_set_context: setting flow not implemented yet"); // FIXME
        assert(0);
    }
    if (src) {
        srcsize->hsize =  src->hsize;
        srcsize->vsize =  src->vsize;
    }
    if (dst) {
        dstsize->hsize =  dst->hsize;
        dstsize->vsize =  dst->vsize;
    }

    ulog_debug(upipe->ulog, "Source size: %zu\t- %zu", srcsize->hsize, srcsize->vsize);
    ulog_debug(upipe->ulog, "Dest size:   %zu\t- %zu", dstsize->hsize, dstsize->vsize);
    // FIXME hardcoded format, algorithm, filters
    upipe_sws->convert_ctx = sws_getCachedContext(upipe_sws->convert_ctx, srcsize->hsize, srcsize->vsize, PIX_FMT_YUV420P, dstsize->hsize, dstsize->vsize, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    if (!upipe_sws->convert_ctx) {
        ulog_error(upipe->ulog, "could not get swscale context");
        return false;
    }

    return true;
}

/** @internal @This configures output flow.
 *
 * @param upipe description structure of the pipe
 * @param flow uref structure describing the flow
 * @return false in case of failure
 */
static bool upipe_sws_conf_outflow(struct upipe *upipe, struct uref *flow)
{
    if (!flow) {
        ulog_error(upipe->ulog, "Invalid flow: null pointer");
        return false;
    }
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    // TODO - detect associated PixelFormat

    uref_pic_size(flow, &upipe_sws->dstsize.hsize, &upipe_sws->dstsize.vsize);
    upipe_sws_set_flow_def(upipe, flow);
    return true;
}


/** @internal @This receives pictures.
 *
 * @param upipe description structure of the pipe
 * @param srcpic uref structure describing the picture
 * @return false in case of failure
 */
static bool upipe_sws_input_pic(struct upipe *upipe, struct uref *srcpic)
{

    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    struct uref *dstpic;
    struct picsize inputsize, *srcsize, *dstsize;
    int strides[4], dstrides[4];
    uint8_t *slices[4], *dslices[4];
    int ret;

    if(unlikely(!upipe_sws->ready)) {
        ulog_warning(upipe->ulog, "pipe not ready");
        uref_release(srcpic);
        return false;
    }
    
    dstsize = &upipe_sws->dstsize;
    dstpic = uref_pic_alloc(upipe_sws->uref_mgr, upipe_sws->ubuf_mgr, dstsize->hsize, dstsize->vsize);
    if (unlikely(dstpic == NULL)) {
        upipe_throw_aerror(upipe);
    }

    uref_pic_size(srcpic, &inputsize.hsize, &inputsize.vsize);
    srcsize = &upipe_sws->srcsize;
    if ( unlikely((srcsize->hsize != inputsize.hsize) || (srcsize->vsize != inputsize.vsize)) )
    {
        ulog_notice(upipe->ulog, "received picture with a new size");
        upipe_sws_set_context(upipe, NULL, &inputsize, NULL);
    }
    
    upipe_sws_filldata(srcpic, upipe_sws->input_flow, strides, slices);
    upipe_sws_filldata(dstpic, upipe_sws->output_flow, dstrides, dslices);

    ret = sws_scale(upipe_sws->convert_ctx, (const uint8_t *const*) slices, strides, 0, srcsize->vsize, dslices, dstrides);

    uref_release(srcpic);
    if(unlikely(ret <= 0)) {
        uref_release(dstpic);
        return false;
    }
    upipe_sws_output(upipe, dstpic);
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_sws_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    const char *flow, *def = NULL, *inflow = NULL; // hush gcc !

    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_release(uref);
       return false;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (upipe_sws->input_flow) {
            ulog_warning(upipe->ulog, "received flow definition without delete first");
            uref_release(upipe_sws->input_flow);
            upipe_sws->input_flow = NULL;
        }

        upipe_sws->input_flow = uref;
        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        return true;
    }

    if (unlikely(upipe_sws->input_flow == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_release(uref);
        return false;
    }

    uref_flow_get_name(upipe_sws->input_flow, &inflow);
    if (unlikely(strcmp(inflow, flow))) {
        ulog_warning(upipe->ulog, "received a buffer not matching the current flow");
        uref_release(uref);
        return false;
    }


    if (unlikely(uref_flow_get_delete(uref))) {
        uref_release(upipe_sws->input_flow);
        upipe_sws->input_flow = NULL;
        uref_release(uref);
        return true;
    }

/*    assert(def);
    if (unlikely(strncmp(def, "pic.", strlen("pic.")))) {
        ulog_warning(upipe->ulog, "received a buffer with an incompatible flow defintion");
        uref_release(uref);
        return false;
    }*/ // FIXME - needed ?

    if (unlikely(uref->ubuf == NULL)) {
        uref_release(uref);
        return true;
    }

    ulog_debug(upipe->ulog, "calling input_pic");
    return upipe_sws_input_pic(upipe, uref);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */

static bool _upipe_sws_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_sws_input(upipe, uref);
    }
    switch (command) {
        // generic linear stuff
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_sws_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_sws_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_LINEAR_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_sws_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_LINEAR_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_sws_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_LINEAR_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_sws_get_output(upipe, p);
        }
        case UPIPE_LINEAR_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_sws_set_output(upipe, output);
        }
        case UPIPE_LINEAR_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_sws_conf_outflow(upipe, flow);
        }
        case UPIPE_LINEAR_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_sws_get_flow_def(upipe, p);
        }
        default:
            return false;
    }
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
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    int ret = _upipe_sws_control(upipe, command, args);
   
    // FIXME - send probes
/*    if (unlikely(upipe_sws->uref_mgr == NULL))
        upipe_throw_need_uref_mgr(upipe);
    else if (unlikely(upipe_sws->ubuf_mgr == NULL))
        upipe_throw_linear_need_ubuf_mgr(upipe);
    else if (unlikely(upipe_sws->output == NULL))
        upipe_throw_new_flow(upipe, NULL, upipe_sws->flow_def);
*/

    // FIXME - check convert_ctx
    if (upipe_sws->output && upipe_sws->uref_mgr && upipe_sws->ubuf_mgr
        && upipe_sws->input_flow && upipe_sws->output_flow) {
        if (!upipe_sws->ready) {
            upipe_sws->ready = true;
            upipe_throw_ready(upipe);
        }
    }
    return ret;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_sws_free(struct upipe *upipe)
{
    struct upipe_sws *upipe_sws = upipe_sws_from_upipe(upipe);
    upipe_sws_clean_output(upipe);
    upipe_sws_clean_ubuf_mgr(upipe);
    upipe_sws_clean_uref_mgr(upipe);
    if (upipe_sws->input_flow) {
        uref_release(upipe_sws->input_flow);
    }
    if (upipe_sws->convert_ctx) {
        sws_freeContext(upipe_sws->convert_ctx);
    }
    free(upipe_sws);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_sws_mgr = {
    /* no need to initialize refcount as we don't use it */

    .upipe_alloc = upipe_sws_alloc,
    .upipe_control = upipe_sws_control,
    .upipe_free = upipe_sws_free,

    .upipe_mgr_free = NULL
};

/** @This sets a new output flow definition along with the
 * output picture size
 *
 * @param upipe description structure of the pipe
 * @param flow output flow definition
 * @param hsize horizontal size
 * @param vsize vertical size
 * @return false in case of error
 */
bool upipe_sws_set_out_flow(struct upipe *upipe, struct uref* flow, uint64_t hsize, uint64_t vsize)
{
    if (! flow) return false;
    uref_pic_set_hsize(&flow, hsize);
    uref_pic_set_vsize(&flow, vsize);
    return upipe_linear_set_flow_def(upipe, flow);
}

/** @This returns the management structure for swscale pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_mgr_alloc(void)
{
    return &upipe_sws_mgr;
}
