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
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
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
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;
    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** swresample context */
    struct SwrContext *swr;

    /** input sample rate */
    uint64_t in_rate;
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
UPIPE_HELPER_FLOW(upipe_swr, UREF_SOUND_FLOW_DEF);
UPIPE_HELPER_OUTPUT(upipe_swr, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_swr, ubuf_mgr);

/** @internal @This receives incoming pictures.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_swr_input_samples(struct upipe *upipe,
                                           struct uref *uref,
                                           struct upump *upump)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    struct ubuf *ubuf;
    const uint8_t *in_buf;
    uint8_t *out_buf;
    uint64_t in_samples, out_samples;
    int size, ret;

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
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
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

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump pump that generated the buffer
 */
static void upipe_swr_input(struct upipe *upipe, struct uref *uref,
                                   struct upump *upump)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);

    /* empty uref */
    if (unlikely(!uref->ubuf)) {
        upipe_swr_output(upipe, uref, upump);
        return;
    }

    /* check ubuf manager */
    if (unlikely(!upipe_swr->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_swr->output_flow);
        if (unlikely(!upipe_swr->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set !");
            uref_free(uref);
            return;
        }
    }

    upipe_swr_input_samples(upipe, uref, upump);
}

/** @This sets the output format.
 *
 * @param upipe description structure of the pipe
 * @param fmt audio format
 * @return false in case of error
 */
static bool _upipe_swr_set_fmt(struct upipe *upipe, const char *fmt)
{
    assert(fmt);

    struct uref *flow;
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    char def[strlen(fmt)+strlen(UREF_SOUND_FLOW_DEF)+1];
    enum AVSampleFormat avfmt;
    
    snprintf(def, sizeof(def), UREF_SOUND_FLOW_DEF"%s", fmt);
    avfmt = upipe_av_samplefmt_from_flow_def(def);

    if (avfmt == AV_SAMPLE_FMT_NONE) {
        upipe_err_va(upipe, "format %s not found", def);
        return false;
    }

    /* reinit swresample context */
    av_opt_set_int(upipe_swr->swr, "out_sample_fmt", avfmt, 0);
    if (swr_init(upipe_swr->swr) < 0) {
        upipe_err_va(upipe, "failed to init swresample with format %s", def);
        return false;
    }

    upipe_swr->out_fmt = avfmt;
    const char * flow_def = upipe_av_samplefmt_to_flow_def(avfmt);

    /* store new flow def */
    flow = uref_dup(upipe_swr->output_flow);
    uref_flow_set_def(flow, flow_def);
    upipe_swr_store_flow_def(upipe, flow);

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

        case UPIPE_SWR_SET_FMT: {
            unsigned int signature = va_arg(args, unsigned int); 
            assert(signature == UPIPE_SWR_SIGNATURE);
            const char *fmt = va_arg(args, const char*);
            return _upipe_swr_set_fmt(upipe, fmt);
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
    const char *def = "(none)";
    int64_t in_layout;
    uint8_t in_chan = 2;
    uint64_t in_rate = 44100;
    enum AVSampleFormat in_fmt;
    struct upipe *upipe = upipe_swr_alloc_flow(mgr, uprobe, signature,
                                               args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);

    /* get sample format */
    uref_flow_get_def(flow_def, &def);
    in_fmt = upipe_av_samplefmt_from_flow_def(def);
    if (unlikely(in_fmt == AV_SAMPLE_FMT_NONE)) {
        upipe_err_va(upipe, "unknown sample format");
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    /* sample rate and channel layout */
    uref_sound_flow_get_rate(flow_def, &in_rate);
    uref_sound_flow_get_channels(flow_def, &in_chan);
    in_layout = av_get_default_channel_layout(in_chan);

    upipe_swr->swr = swr_alloc_set_opts(NULL,
                in_layout, in_fmt, in_rate,
                in_layout, in_fmt, in_rate,
                0, NULL);

    if (unlikely(!upipe_swr->swr)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    if (swr_init(upipe_swr->swr) < 0) {
        upipe_err_va(upipe, "failed to init swresample with format %s", def);
        return false;
    }

    /* store output configuration and flow def */
    upipe_swr->out_chan = in_chan;
    upipe_swr->out_rate = in_rate;
    upipe_swr->out_fmt = in_fmt;
    uref_sound_flow_set_rate(flow_def, upipe_swr->out_rate);
    uref_sound_flow_set_channels(flow_def, upipe_swr->out_chan);

    upipe_swr_init_ubuf_mgr(upipe);
    upipe_swr_init_output(upipe);
    upipe_swr_store_flow_def(upipe, flow_def);

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
    if (likely(upipe_swr->swr)) {
        swr_free(&upipe_swr->swr);
    }

    upipe_throw_dead(upipe);
    upipe_swr_clean_output(upipe);
    upipe_swr_clean_ubuf_mgr(upipe);
    upipe_swr_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_swr_mgr = {
    .signature = UPIPE_SWR_SIGNATURE,

    .upipe_alloc = upipe_swr_alloc,
    .upipe_input = upipe_swr_input,
    .upipe_control = upipe_swr_control,
    .upipe_free = upipe_swr_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for swresample pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_swr_mgr_alloc(void)
{
    return &upipe_swr_mgr;
}

