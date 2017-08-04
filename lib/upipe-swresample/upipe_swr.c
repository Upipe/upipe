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
 * @short Upipe swresample (ffmpeg) module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
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
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

/** typical frame size for latency calculation */
#define FRAME_SIZE 1152

/** @hidden */
static bool upipe_swr_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p);
/** @hidden */
static int upipe_swr_check(struct upipe *upipe, struct uref *flow_format);

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
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** output pipe */
    struct upipe *output;

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

    /** swresample context */
    struct SwrContext *swr;

    /** number of planes in input */
    uint8_t in_planes;
    /** number of planes in output */
    uint8_t out_planes;
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
UPIPE_HELPER_OUTPUT(upipe_swr, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_swr, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_swr, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_swr_check,
                      upipe_swr_register_output_request,
                      upipe_swr_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_swr, urefs, nb_urefs, max_urefs, blockers, upipe_swr_handle)

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_swr_handle(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_swr_store_flow_def(upipe, NULL);

        uref_sound_flow_get_planes(uref, &upipe_swr->in_planes);
        uint8_t in_chan = 0;
        uint64_t in_rate = 0;
        enum AVSampleFormat in_fmt =
            upipe_av_samplefmt_from_flow_def(uref, &in_chan);
        uref_sound_flow_get_rate(uref, &in_rate);

        if (upipe_swr->out_rate != 0 && in_rate != 0 &&
            upipe_swr->out_rate != in_rate) {
            uint64_t latency = 0;
            uref_clock_get_latency(uref, &latency);
            uref_clock_set_latency(uref,
                    latency + FRAME_SIZE * UCLOCK_FREQ / in_rate);
        }

        av_opt_set_int(upipe_swr->swr, "in_sample_fmt", in_fmt, 0);
        av_opt_set_int(upipe_swr->swr, "used_channel_count", 0, 0);
        av_opt_set_int(upipe_swr->swr, "in_channel_count", in_chan, 0);
        av_opt_set_int(upipe_swr->swr, "in_channel_layout",
                       av_get_default_channel_layout(in_chan), 0);
        av_opt_set_int(upipe_swr->swr, "in_sample_rate", in_rate, 0);

        /* set missing output options */
        if (upipe_swr->out_fmt == AV_SAMPLE_FMT_NONE) {
            av_opt_set_int(upipe_swr->swr, "out_sample_fmt", in_fmt, 0);
        }
        if (upipe_swr->out_chan == 0) {
            av_opt_set_int(upipe_swr->swr, "out_channel_count", in_chan, 0);
            av_opt_set_int(upipe_swr->swr, "out_channel_layout",
                           av_get_default_channel_layout(in_chan), 0);
        }
        if (upipe_swr->out_rate == 0) {
            av_opt_set_int(upipe_swr->swr, "out_sample_rate", in_rate, 0);
        }

        /* reinit swresample context */
        if (swr_init(upipe_swr->swr) < 0) {
            upipe_err_va(upipe, "failed to init swresample with format %s",
                         def);
            upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
        }

        uref = upipe_swr_store_flow_def_input(upipe, uref);
        upipe_swr_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_swr->flow_def == NULL)
        return false;

    struct ubuf *ubuf;
    size_t in_samples;
    uint64_t out_samples;
    uint8_t out_planes;
    int64_t in_rate, out_rate;
    int ret;

    if (unlikely(!ubase_check(uref_sound_size(uref, &in_samples, NULL)))) {
        uref_free(uref);
        return true;
    }

    /* conversion sample rates */
    av_opt_get_int(upipe_swr->swr, "in_sample_rate", 0, &in_rate);
    av_opt_get_int(upipe_swr->swr, "out_sample_rate", 0, &out_rate);

    /* out samples (needed for resampling) */
    out_samples = av_rescale_rnd(
                    swr_get_delay(upipe_swr->swr, in_rate) + in_samples,
                    out_rate, in_rate, AV_ROUND_UP);
    //upipe_verbose_va(upipe, "in: %zu out: %"PRIu64, in_samples, out_samples);

    /* compute delay (see swresample.h for timebase) */
    uint64_t delay = swr_get_delay(upipe_swr->swr, UCLOCK_FREQ);

    const uint8_t *in_buf[upipe_swr->in_planes];
    if (unlikely(!ubase_check(uref_sound_read_uint8_t(uref, 0, -1, in_buf,
                                                    upipe_swr->in_planes)))) {
        upipe_err(upipe, "could not read uref, dropping samples");
        uref_free(uref);
        return true;
    }

    /* allocate output ubuf */
    out_planes = upipe_swr->out_planes ?
                  upipe_swr->out_planes : upipe_swr->in_planes;

    ubuf = ubuf_sound_alloc(upipe_swr->ubuf_mgr, out_samples);
    if (unlikely(!ubuf)) {
        uref_sound_unmap(uref, 0, -1, upipe_swr->in_planes);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    uint8_t *out_buf[out_planes];
    if (unlikely(!ubase_check(ubuf_sound_write_uint8_t(ubuf, 0, -1, out_buf,
                                                             out_planes)))) {
        upipe_err(upipe, "could not write uref, dropping samples");
        ubuf_free(ubuf);
        uref_sound_unmap(uref, 0, -1, upipe_swr->in_planes);
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    /* fire! */
    ret = swr_convert(upipe_swr->swr, out_buf, out_samples,
                                      in_buf, in_samples);

    ubuf_sound_unmap(ubuf, 0, -1, out_planes);
    uref_sound_unmap(uref, 0, -1, upipe_swr->in_planes);
    ubuf_free(uref_detach_ubuf(uref));
    uref_attach_ubuf(uref, ubuf);

    if (ret < 0) {
        upipe_err(upipe, "error during swresample conversion");
        uref_free(uref);
        return true;
    }
    out_samples = ret;

    /* set new samples count and resize ubuf */
    uref_sound_flow_set_samples(uref, out_samples);
    uref_sound_resize(uref, 0, out_samples);

    /* set new pts and rebase */
    uint64_t pts;
    if (likely(ubase_check(uref_clock_get_pts_sys(uref, &pts))))
        uref_clock_set_pts_sys(uref, pts - delay);
    if (likely(ubase_check(uref_clock_get_pts_prog(uref, &pts))))
        uref_clock_set_pts_prog(uref, pts - delay);
    if (likely(ubase_check(uref_clock_get_pts_orig(uref, &pts))))
        uref_clock_set_pts_orig(uref, pts - delay);

    upipe_swr_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_swr_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_swr_check_input(upipe)) {
        upipe_swr_hold_input(upipe, uref);
        upipe_swr_block_input(upipe, upump_p);
    } else if (!upipe_swr_handle(upipe, uref, upump_p)) {
        upipe_swr_hold_input(upipe, uref);
        upipe_swr_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_swr_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_swr *upipe_swr = upipe_swr_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_swr_store_flow_def(upipe, flow_format);

    if (upipe_swr->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_swr_check_input(upipe);
    upipe_swr_output_input(upipe);
    upipe_swr_unblock_input(upipe);
    if (was_buffered && upipe_swr_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_swr_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_swr_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    uint8_t in_planes;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (unlikely(ubase_ncmp(def, UREF_SOUND_FLOW_DEF) ||
                 !ubase_check(uref_sound_flow_get_planes(flow_def,
                                                         &in_planes))))
        return UBASE_ERR_INVALID;

    uint8_t in_chan;
    uint64_t in_rate;
    enum AVSampleFormat in_fmt =
        upipe_av_samplefmt_from_flow_def(flow_def, &in_chan);
    if (in_fmt == AV_SAMPLE_FMT_NONE
        || !ubase_check(uref_sound_flow_get_rate(flow_def, &in_rate))) {
        upipe_err(upipe, "incompatible flow def");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
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
static int upipe_swr_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        /* generic commands */
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_swr_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_swr_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_GET_FLOW_DEF:
            return upipe_swr_control_output(upipe, command, args);
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_swr_set_flow_def(upipe, flow);
        }

        default:
            return UBASE_ERR_UNHANDLED;
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

    upipe_swr->out_rate = 0;
    upipe_swr->out_chan = 0;
    upipe_swr->out_planes = 0;
    upipe_swr->out_fmt = AV_SAMPLE_FMT_NONE;

    /* get sample format */
    const char *def = "(none)";
    uref_flow_get_def(flow_def, &def);
    upipe_swr->out_fmt = upipe_av_samplefmt_from_flow_def(flow_def,
            &upipe_swr->out_chan);
    if (upipe_swr->out_fmt == AV_SAMPLE_FMT_NONE) {
        uref_flow_delete_def(flow_def);
        def = "(none)";
    }
    if (!upipe_swr->out_chan) {
        uref_sound_flow_delete_channels(flow_def);
    }
    uint8_t sample_size = 0;
    uref_sound_flow_get_sample_size(flow_def, &sample_size);
    if (!sample_size) {
        uref_sound_flow_delete_sample_size(flow_def);
    }

    /* samplerate and planes */
    uref_sound_flow_get_rate(flow_def, &upipe_swr->out_rate);
    if (!upipe_swr->out_rate) {
        uref_sound_flow_delete_rate(flow_def);
    }
    uref_sound_flow_get_planes(flow_def, &upipe_swr->out_planes);
    if (!upipe_swr->out_planes) {
        uref_sound_flow_delete_planes(flow_def);
    }

    /* default channel layout for given channel number */
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

    /* if samplerate/planes/format are present, try swr init */
    if (upipe_swr->out_fmt != AV_SAMPLE_FMT_NONE
        && upipe_swr->out_rate != 0
        && upipe_swr->out_planes != 0
        && (swr_init(upipe_swr->swr) < 0)) {
        upipe_err_va(upipe, "failed to init swresample with format %s", def);
        uref_free(flow_def);
        upipe_swr_free_flow(upipe);
        return NULL;
    }

    upipe_swr_init_urefcount(upipe);
    upipe_swr_init_ubuf_mgr(upipe);
    upipe_swr_init_output(upipe);
    upipe_swr_init_flow_def(upipe);
    upipe_swr_init_input(upipe);
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
    upipe_swr_clean_input(upipe);
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
    .upipe_control = upipe_swr_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for swresample pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_swr_mgr_alloc(void)
{
    return &upipe_swr_mgr;
}

