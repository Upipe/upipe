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
 * @short Upipe swresample (ffmpeg) module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-av/upipe_av_samplefmt.h>
#include <upipe-swresample/upipe_swr.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

/** upipe_swr structure with swresample parameters */ 
struct upipe_swr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** output flow */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** swresample context */
    struct SwrContext *swr;

    /** output sample rate */
    uint64_t out_rate;
    /** output channels number */
    uint8_t out_chan;
    /** output format */
    enum AVSampleFormat out_fmt;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_swr, upipe, UPIPE_SWR_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_swr, urefcount, upipe_swr_free)
UPIPE_HELPER_FLOW(upipe_swr, UREF_SOUND_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_swr, output, flow_def, flow_def_sent)
UPIPE_HELPER_FLOW_DEF(upipe_swr, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_swr, ubuf_mgr);

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_swr_input(struct upipe *upipe, struct uref *uref,
                            struct upump *upump)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    struct ubuf *ubuf;
    const uint8_t *in_buf;
    uint8_t *out_buf;
    uint64_t in_samples, out_samples;
    int size, ret;

    /* check ubuf manager */
    if (unlikely(!upipe_swr->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_swr->flow_def_attr);
        if (unlikely(!upipe_swr->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set !");
            uref_free(uref);
            return;
        }
    }

    if (unlikely(!uref_sound_flow_get_samples(uref, &in_samples))) {
        uref_free(uref);
        return;
    }
    /* TODO compute out samples and accurate pts.
     * This is fine with format conversion but not with resampling. */
    out_samples = in_samples;

    size = -1;
    if (unlikely(!uref_block_read(uref, 0, &size, &in_buf))) {
        upipe_err(upipe, "could not read uref, dropping samples");
        uref_free(uref);
        return;
    }

    /* allocate output ubuf */
    ubuf = ubuf_block_alloc(upipe_swr->ubuf_mgr, 
            av_samples_get_buffer_size(NULL, upipe_swr->out_chan,
                                       out_samples, upipe_swr->out_fmt, 1));
    if (unlikely(!ubuf)) {
        uref_block_unmap(uref, 0);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    size = -1;
    ubuf_block_write(ubuf, 0, &size, &out_buf);

    /* fire! */
    ret = swr_convert(upipe_swr->swr, &out_buf, out_samples,
                                      &in_buf, in_samples);

    ubuf_block_unmap(ubuf, 0);
    uref_block_unmap(uref, 0);
    ubuf_free(uref_detach_ubuf(uref));
    uref_attach_ubuf(uref, ubuf);

    if (ret < 0) {
        upipe_err(upipe, "error during swresample conversion");
        uref_free(uref);
        return;
    }
    out_samples = ret;

    /* set new samples count and resize ubuf */
    uref_sound_flow_set_samples(uref, out_samples);
    uref_block_resize(uref, 0,
            av_samples_get_buffer_size(NULL, upipe_swr->out_chan,
                                       out_samples, upipe_swr->out_fmt, 1));

    upipe_swr_output(upipe, uref, upump);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static bool upipe_swr_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;

    const char *def;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 ubase_ncmp(def, UREF_SOUND_FLOW_DEF)))
        return false;

    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    enum AVSampleFormat in_fmt =
        upipe_av_samplefmt_from_flow_def(def);
    if (in_fmt == AV_SAMPLE_FMT_NONE) {
        upipe_err(upipe, "incompatible flow def");
        uref_dump(flow_def, upipe->uprobe);
        return false;
    }

    /* reinit swresample context */
    av_opt_set_int(upipe_swr->swr, "in_sample_fmt", in_fmt, 0);
    if (swr_init(upipe_swr->swr) < 0) {
        upipe_err_va(upipe, "failed to init swresample with format %s", def);
        return false;
    }
    /* TODO : check that sample rates and channels are identical */

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    flow_def = upipe_swr_store_flow_def_input(upipe, flow_def);
    if (flow_def != NULL)
        upipe_swr_store_flow_def(upipe, flow_def);
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
static bool upipe_swr_control(struct upipe *upipe,
                                     enum upipe_command command, va_list args)
{
    switch (command) {
        /* generic commands */
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_swr_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_swr_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_swr_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_swr_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_swr_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_swr_set_flow_def(upipe, flow);
        }

        default:
            return false;
    }
}

/** @internal @This allocates a swresample pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_swr_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_swr_alloc_flow(mgr, uprobe, signature,
                                               args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);

    /* get sample format */
    const char *def = "(none)";
    uref_flow_get_def(flow_def, &def);
    upipe_swr->out_fmt = upipe_av_samplefmt_from_flow_def(def);
    if (unlikely(upipe_swr->out_fmt == AV_SAMPLE_FMT_NONE ||
                 !uref_sound_flow_get_rate(flow_def, &upipe_swr->out_rate) ||
                 !uref_sound_flow_get_channels(flow_def,
                                               &upipe_swr->out_chan))) {
        uref_free(flow_def);
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    /* sample rate and channel layout */
    int64_t out_layout = av_get_default_channel_layout(upipe_swr->out_chan);

    upipe_swr->swr = swr_alloc_set_opts(NULL,
                out_layout, upipe_swr->out_fmt, upipe_swr->out_rate,
                out_layout, upipe_swr->out_fmt, upipe_swr->out_rate,
                0, NULL);

    if (unlikely(!upipe_swr->swr)) {
        uref_free(flow_def);
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    if (swr_init(upipe_swr->swr) < 0) {
        upipe_err_va(upipe, "failed to init swresample with format %s", def);
        uref_free(flow_def);
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    upipe_swr_init_urefcount(upipe);
    upipe_swr_init_ubuf_mgr(upipe);
    upipe_swr_init_output(upipe);
    upipe_swr_init_flow_def(upipe);
    upipe_swr_store_flow_def_attr(upipe, flow_def);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_swr_free(struct upipe *upipe)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    if (likely(upipe_swr->swr))
        swr_free(&upipe_swr->swr);

    upipe_throw_dead(upipe);
    upipe_swr_clean_output(upipe);
    upipe_swr_clean_flow_def(upipe);
    upipe_swr_clean_ubuf_mgr(upipe);
    upipe_swr_clean_urefcount(upipe);
    upipe_swr_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_swr_mgr = {
    .refcount = NULL,
    .signature = UPIPE_SWR_SIGNATURE,

    .upipe_alloc = upipe_swr_alloc,
    .upipe_input = upipe_swr_input,
    .upipe_control = upipe_swr_control
};

/** @This returns the management structure for swresample pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_swr_mgr_alloc(void)
{
    return &upipe_swr_mgr;
}

