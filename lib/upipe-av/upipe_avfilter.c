/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 * Copyright (C) 2019-2020 EasyTools
 *
 * Authors: Clément Vasseur
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
 * @short Upipe avfilter module
 */

#define _GNU_SOURCE

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe-av/upipe_avfilter.h>
#include <upipe-av/upipe_av_pixfmt.h>
#include <upipe-av/upipe_av_samplefmt.h>
#include <upipe-av/uref_avfilter_flow.h>
#include <upipe-av/ubuf_av.h>

#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>

/** @internal @This enumerates the avfilter sub pipe private events. */
enum uprobe_avfilt_sub_event  {
    /** sentinel */
    UPROBE_AVFILT_SUB_SENTINEL = UPROBE_LOCAL,
    /** the filter was updated (void) */
    UPROBE_AVFILT_SUB_UPDATE,
};

/** @internal @This enumerates the supported media types. */
enum upipe_avfilt_sub_media_type {
    /** unknown */
    UPIPE_AVFILT_SUB_MEDIA_TYPE_UNKNOWN,
    /** video */
    UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO,
    /** audio */
    UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO,
};

/** @This is the sub pipe private structure of the avfilter pipe. */
struct upipe_avfilt_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** public upipe structure */
    struct upipe upipe;
    /** chain in the super pipe list */
    struct uchain uchain;
    /** allocation flow definition */
    struct uref *flow_def_alloc;
    /** output pipe */
    struct upipe *output;
    /** output flow def */
    struct uref *flow_def;
    /** output internal state */
    enum upipe_helper_output_state output_state;
    /** registered requests on output */
    struct uchain requests;
    /** AVFrame buffer manager */
    struct ubuf_mgr *ubuf_mgr;
    /** uclock request */
    struct urequest uclock_request;
    /** uclock */
    struct uclock *uclock;
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;
    /** sub pipe name */
    const char *name;
    /** sub pipe is an input pipe */
    bool input;
    /** avfilter buffer source */
    AVFilterContext *buffer_ctx;
    /** system clock offset */
    uint64_t pts_sys_offset;
    /** prog clock offset */
    uint64_t pts_prog_offset;
    /** first pts_prog */
    uint64_t first_pts_prog;
    /** last pts */
    uint64_t last_pts_prog;
    /** last duration */
    uint64_t last_duration;
    /** list of retained uref */
    struct uchain urefs;
    /** warn once on unconfigured filter graph */
    bool warn_not_configured;

    /** media type configured at allocation */
    enum upipe_avfilt_sub_media_type media_type;
    /** media type private fields */
    union {
        /** video */
        struct {
            /** format */
            enum AVPixelFormat pix_fmt;
            /** input width */
            uint64_t width;
            /** input height */
            uint64_t height;
            /** chroma map */
            const char *chroma_map[UPIPE_AV_MAX_PLANES];
            /** framerate */
            struct urational fps;
            /** sample aspect ratio */
            struct urational sar;
            /** interlaced? */
            bool interlaced;
        } video;
        /** audio */
        struct {
            /** sample format */
            enum AVSampleFormat sample_fmt;
            /** number of channels */
            uint8_t channels;
            /** channel layout */
            uint64_t channel_layout;
            /** sample rate */
            uint64_t sample_rate;
        } audio;
    };
};

/** @hidden */
static void upipe_avfilt_reset(struct upipe *upipe);
/** @hidden */
static inline void upipe_avfilt_sub_flush_cb(struct upump *upump);
/** @hidden */
static void upipe_avfilt_sub_pop(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_avfilt_sub, upipe, UPIPE_AVFILT_SUB_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_avfilt_sub, NULL)
UPIPE_HELPER_UREFCOUNT(upipe_avfilt_sub, urefcount, upipe_avfilt_sub_free)
UPIPE_HELPER_OUTPUT(upipe_avfilt_sub, output, flow_def, output_state, requests)
UPIPE_HELPER_UCLOCK(upipe_avfilt_sub, uclock, uclock_request,
                    NULL,
                    upipe_avfilt_sub_register_output_request,
                    upipe_avfilt_sub_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_avfilt_sub, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_avfilt_sub, upump, upump_mgr);

/** upipe_avfilt structure */
struct upipe_avfilt {
    /** refcount management structure */
    struct urefcount urefcount;

    /** sub pipe manager */
    struct upipe_mgr sub_mgr;
    /** sub pipe list */
    struct uchain subs;

    /** filter graph description */
    char *filters_desc;
    /** avfilter filter graph */
    AVFilterGraph *filter_graph;
    /** filter graph is configured? */
    bool configured;

    /** AVFrame ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_avfilt_init_filters(struct upipe *upipe);
/** @hidden */
static void upipe_avfilt_clean_filters(struct upipe *upipe);
/** @hidden */
static void upipe_avfilt_update_outputs(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_avfilt, upipe, UPIPE_AVFILT_SIGNATURE)
UPIPE_HELPER_VOID(upipe_avfilt)
UPIPE_HELPER_UREFCOUNT(upipe_avfilt, urefcount, upipe_avfilt_free)
UPIPE_HELPER_SUBPIPE(upipe_avfilt, upipe_avfilt_sub, sub,
                     sub_mgr, subs, uchain);

/** @internal @This is the avbuffer free callback.
 *
 * @param opaque pointer to the uref
 * @param data avbuffer data
 */
static void buffer_free_pic_cb(void *opaque, uint8_t *data)
{
    struct uref *uref = opaque;

    uint64_t buffers;
    if (unlikely(!ubase_check(uref_attr_get_priv(uref, &buffers))))
        return;
    if (--buffers) {
        uref_attr_set_priv(uref, buffers);
        return;
    }

    const char *chroma;
    uref_pic_foreach_plane(uref, chroma) {
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
    }

    uref_free(uref);
}

/** @internal @This is the avbuffer free callback for uref sound.
 *
 * @param opaque pointer to the uref
 * @param data avbuffer data
 */
static void buffer_free_sound_cb(void *opaque, uint8_t *data)
{
    struct uref *uref = opaque;

    uint64_t buffers;
    if (unlikely(!ubase_check(uref_attr_get_priv(uref, &buffers))))
        return;
    if (--buffers) {
        uref_attr_set_priv(uref, buffers);
        return;
    }

    const char *channel;
    uref_sound_foreach_plane(uref, channel) {
        uref_sound_plane_unmap(uref, channel, 0, -1);
    }

    uref_free(uref);
}

/** @internal @This makes an urational from an AVRational.
 *
 * @param v AVRational to convert
 * @return an urational
 */
static inline struct urational urational(AVRational v)
{
    return (struct urational){ .num = v.num, .den = v.den };
}

/** @internal @This throws an update event for avfilter sub pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_avfilt_sub_throw_update(struct upipe *upipe)
{
    return upipe_throw(upipe, UPROBE_AVFILT_SUB_UPDATE,
                       UPIPE_AVFILT_SUB_SIGNATURE);
}

/** @internal @This checks if the flow definition match the avfilter buffer.
 *
 * @param upipe description structure of the sub pipe
 * @return true if the flow def match the buffer
 */
static bool upipe_avfilt_sub_check_flow_def(struct upipe *upipe,
                                            const AVFrame *frame)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    enum AVMediaType media_type =
        av_buffersink_get_type(upipe_avfilt_sub->buffer_ctx);

    if (unlikely(!upipe_avfilt_sub->flow_def))
        return false;

    switch (media_type) {
        case AVMEDIA_TYPE_VIDEO: {
            enum AVPixelFormat pix_fmt =
                av_buffersink_get_format(upipe_avfilt_sub->buffer_ctx);
            int width = av_buffersink_get_w(upipe_avfilt_sub->buffer_ctx);
            int height = av_buffersink_get_h(upipe_avfilt_sub->buffer_ctx);
            AVRational av_fps =
                av_buffersink_get_frame_rate(upipe_avfilt_sub->buffer_ctx);
            if (!av_fps.den)
                av_fps = av_inv_q(av_buffersink_get_time_base(
                        upipe_avfilt_sub->buffer_ctx));
            struct urational fps;
            fps.num = av_fps.num;
            fps.den = av_fps.den;
            struct urational sar =
                urational(
                    av_buffersink_get_sample_aspect_ratio(
                        upipe_avfilt_sub->buffer_ctx));
            bool interlaced = frame->interlaced_frame;

            if (upipe_avfilt_sub->video.pix_fmt == pix_fmt &&
                upipe_avfilt_sub->video.width == width &&
                upipe_avfilt_sub->video.height == height &&
                !urational_cmp(&upipe_avfilt_sub->video.fps, &fps) &&
                !urational_cmp(&upipe_avfilt_sub->video.sar, &sar) &&
                upipe_avfilt_sub->video.interlaced == interlaced)
                return true;
            return false;
        }

        case AVMEDIA_TYPE_AUDIO: {
            enum AVSampleFormat sample_fmt =
                av_buffersink_get_format(upipe_avfilt_sub->buffer_ctx);
            int channels =
                av_buffersink_get_channels(upipe_avfilt_sub->buffer_ctx);
            uint64_t channel_layout =
                av_buffersink_get_channel_layout(upipe_avfilt_sub->buffer_ctx);
            int sample_rate =
                av_buffersink_get_sample_rate(upipe_avfilt_sub->buffer_ctx);

            if (upipe_avfilt_sub->audio.sample_fmt == sample_fmt &&
                upipe_avfilt_sub->audio.channels == channels &&
                upipe_avfilt_sub->audio.channel_layout == channel_layout &&
                upipe_avfilt_sub->audio.sample_rate == sample_rate)
                return true;
            return false;
        }

        default:
            break;
    }
    return false;
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_input description structure of the pipe
 * @return an error code
 */
static int upipe_avfilt_sub_build_flow_def(struct upipe *upipe,
                                           const AVFrame *frame,
                                           struct uref *flow_def)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    enum AVMediaType media_type =
        av_buffersink_get_type(upipe_avfilt_sub->buffer_ctx);

    switch (media_type) {
        case AVMEDIA_TYPE_VIDEO: {
            enum AVPixelFormat pix_fmt =
                av_buffersink_get_format(upipe_avfilt_sub->buffer_ctx);
            int width = av_buffersink_get_w(upipe_avfilt_sub->buffer_ctx);
            int height = av_buffersink_get_h(upipe_avfilt_sub->buffer_ctx);
            AVRational fps =
                av_buffersink_get_frame_rate(upipe_avfilt_sub->buffer_ctx);
            if (!fps.den)
                fps = av_inv_q(av_buffersink_get_time_base(
                        upipe_avfilt_sub->buffer_ctx));
            AVRational sar =
                av_buffersink_get_sample_aspect_ratio(
                    upipe_avfilt_sub->buffer_ctx);
            bool interlaced = frame->interlaced_frame;

            if (width < 0 || height < 0)
                return UBASE_ERR_INVALID;

            UBASE_RETURN(upipe_av_pixfmt_to_flow_def(pix_fmt, flow_def))
            UBASE_RETURN(uref_pic_flow_set_hsize(flow_def, width))
            UBASE_RETURN(uref_pic_flow_set_vsize( flow_def, height))
            UBASE_RETURN(uref_pic_flow_set_fps(flow_def, urational(fps)))
            UBASE_RETURN(uref_pic_flow_set_sar(flow_def, urational(sar)))
            if (!interlaced)
                UBASE_RETURN(uref_pic_set_progressive(flow_def));

            upipe_avfilt_sub->video.pix_fmt =
                upipe_av_pixfmt_from_flow_def(
                    flow_def, NULL, upipe_avfilt_sub->video.chroma_map);
            upipe_avfilt_sub->video.width = width;
            upipe_avfilt_sub->video.height = height;
            upipe_avfilt_sub->video.fps = urational(fps);
            upipe_avfilt_sub->video.sar = urational(sar);
            upipe_avfilt_sub->video.interlaced = interlaced;
            return UBASE_ERR_NONE;
        }

        case AVMEDIA_TYPE_AUDIO: {
            enum AVSampleFormat sample_fmt =
                av_buffersink_get_format(upipe_avfilt_sub->buffer_ctx);
            int channels =
                av_buffersink_get_channels(upipe_avfilt_sub->buffer_ctx);
            uint64_t channel_layout =
                av_buffersink_get_channel_layout(upipe_avfilt_sub->buffer_ctx);
            int sample_rate =
                av_buffersink_get_sample_rate(upipe_avfilt_sub->buffer_ctx);
            upipe_avfilt_sub->audio.sample_fmt = sample_fmt;
            upipe_avfilt_sub->audio.channels = channels;
            upipe_avfilt_sub->audio.channel_layout = channel_layout;
            upipe_avfilt_sub->audio.sample_rate = sample_rate;
            return upipe_av_samplefmt_to_flow_def(flow_def, sample_fmt,
                                                  channels);
        }

        default:
            break;
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This waits for the next uref to output.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_sub_wait(struct upipe *upipe, uint64_t timeout)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    if (!upipe_avfilt_sub->upump_mgr)
        return;
    return upipe_avfilt_sub_wait_upump(upipe, timeout, upipe_avfilt_sub_flush_cb);
}

/** @internal @This outputs the retained urefs.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_sub_flush(struct upipe *upipe)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    uint64_t now = upipe_avfilt_sub_now(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_avfilt_sub->urefs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t pts_sys = UINT64_MAX;
        uref_clock_get_pts_sys(uref, &pts_sys);
        if (now == UINT64_MAX || pts_sys == UINT64_MAX || pts_sys <= now) {
            ulist_delete(uchain);
            upipe_avfilt_sub_output(upipe, uref, &upipe_avfilt_sub->upump);
        }
        else {
            upipe_avfilt_sub_wait(upipe, pts_sys - now);
            return;
        }
    }

    upipe_avfilt_sub_pop(upipe);
}

/** @internal @This pushes an uref to the output queue.
 *
 * @param upipe description structure of the pipe
 * @param uref uref to output
 */
static void upipe_avfilt_sub_push(struct upipe *upipe, struct uref *uref)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    ulist_add(&upipe_avfilt_sub->urefs, uref_to_uchain(uref));
    upipe_avfilt_sub_wait(upipe, 0);
}

/** @internal @This is the callback to output retained urefs.
 *
 * @param upump description structure of the pump
 */
static void upipe_avfilt_sub_flush_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    return upipe_avfilt_sub_flush(upipe);
}

/** @internal @This outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param frame AVFrame to output
 * @param return true if a packet was outputted
 */
static bool upipe_avfilt_sub_output_frame(struct upipe *upipe, AVFrame *frame)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    if (unlikely(!upipe_avfilt_sub_check_flow_def(upipe, frame)))
        upipe_avfilt_sub_store_flow_def(upipe, NULL);

    if (unlikely(!upipe_avfilt_sub->flow_def)) {
        struct uref *flow_def_dup = uref_dup(upipe_avfilt_sub->flow_def_alloc);
        if (unlikely(!flow_def_dup)) {
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            return false;
        }

        int ret = upipe_avfilt_sub_build_flow_def(upipe, frame, flow_def_dup);
        if (unlikely(!ubase_check(ret))) {
            uref_free(flow_def_dup);
            upipe_throw_error(upipe, ret);
            return false;
        }
        uref_sound_flow_set_rate(flow_def_dup, frame->sample_rate);
        upipe_avfilt_sub_store_flow_def(upipe, flow_def_dup);
    }

    if (unlikely(!upipe_avfilt_sub->ubuf_mgr)) {
        upipe_warn(upipe, "no ubuf manager for now");
        return false;
    }

    enum upipe_avfilt_sub_media_type type;

    struct ubuf *ubuf = NULL;
    if (ubase_check(uref_flow_match_def(
                upipe_avfilt_sub->flow_def, UREF_PIC_FLOW_DEF))) {
        ubuf = ubuf_pic_av_alloc(upipe_avfilt_sub->ubuf_mgr, frame);
        type = UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO;
    }
    else if (ubase_check(uref_flow_match_def(
                upipe_avfilt_sub->flow_def, UREF_SOUND_FLOW_DEF))) {
        ubuf = ubuf_sound_av_alloc(upipe_avfilt_sub->ubuf_mgr, frame);
        type = UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO;
    }
    else {
        upipe_warn(upipe, "unsupported flow format");
        return false;
    }

    if (unlikely(ubuf == NULL)) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    struct uref *uref = uref_sibling_alloc_control(
        upipe_avfilt_sub->flow_def_alloc);
    if (unlikely(!uref)) {
        ubuf_free(ubuf);
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    uref_attach_ubuf(uref, ubuf);

    /* get system time */
    uint64_t now = upipe_avfilt_sub_now(upipe);

    /* set pts orig */
    struct urational to = { .num = UCLOCK_FREQ, .den = 1 };
    AVRational av_fps = av_buffersink_get_time_base(
        upipe_avfilt_sub->buffer_ctx);
    struct urational from = { .num = av_fps.num, .den = av_fps.den };
    struct urational mult = urational_multiply(&to, &from);
    uint64_t pts_orig = frame->pts * mult.num / mult.den;
    uint64_t pts_prog = pts_orig + upipe_avfilt_sub->pts_prog_offset;
    if (upipe_avfilt_sub->last_pts_prog != UINT64_MAX) {
        if (pts_prog <= upipe_avfilt_sub->last_pts_prog) {
            upipe_warn(upipe, "pts in the past, resetting");
            upipe_avfilt_sub->pts_sys_offset = UINT64_MAX;
            upipe_avfilt_sub->pts_prog_offset +=
                upipe_avfilt_sub->last_pts_prog - pts_prog + 1;
            pts_prog += upipe_avfilt_sub->last_pts_prog - pts_prog + 1;
        }
    }
    upipe_avfilt_sub->last_pts_prog = pts_prog;

    if (upipe_avfilt_sub->pts_sys_offset == UINT64_MAX) {
        upipe_avfilt_sub->pts_sys_offset = now;
        upipe_avfilt_sub->first_pts_prog = pts_prog;
    }
    uint64_t pts_sys = UINT64_MAX;
    if (upipe_avfilt_sub->pts_sys_offset != UINT64_MAX) {
        pts_sys = upipe_avfilt_sub->pts_sys_offset +
            pts_prog - upipe_avfilt_sub->first_pts_prog;
    }

    uref_clock_set_pts_orig(uref, pts_orig);
    uref_clock_set_pts_prog(uref, pts_prog);
    if (pts_sys != UINT64_MAX)
        uref_clock_set_pts_sys(uref, pts_sys);

    uint64_t duration = 0;
    switch (type) {
        case UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO:
            duration = frame->pkt_duration;
            UBASE_ERROR(upipe, uref_pic_set_number(
                    uref, frame->coded_picture_number))

            if (!frame->interlaced_frame)
                UBASE_ERROR(upipe, uref_pic_set_progressive(uref))
            else if (frame->top_field_first)
                UBASE_ERROR(upipe, uref_pic_set_tff(uref))

            if (frame->key_frame)
                UBASE_ERROR(upipe, uref_pic_set_key(uref))

            break;
        case UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO:
            duration = frame->nb_samples * UCLOCK_FREQ / frame->sample_rate;
            break;

        case UPIPE_AVFILT_SUB_MEDIA_TYPE_UNKNOWN:
            upipe_err(upipe, "unknown media type");
            break;
    }
    upipe_avfilt_sub->last_duration = duration;
    UBASE_ERROR(upipe, uref_clock_set_duration(uref, duration));

    upipe_verbose_va(upipe, "output frame %d(%d) %ix%i pts_prog=%f "
                     "pts_sys=%f duration=%f",
                     frame->display_picture_number,
                     frame->coded_picture_number,
                     frame->width, frame->height,
                     (double) pts_prog / UCLOCK_FREQ,
                     (double) pts_sys / UCLOCK_FREQ,
                     (double) duration / UCLOCK_FREQ);

    upipe_avfilt_sub_push(upipe, uref);
    return ulist_empty(&upipe_avfilt_sub->urefs);
}

/** @internal @This checks for frame to output.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_sub_pop(struct upipe *upipe)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    if (upipe_avfilt_sub->input)
        return;

    if (unlikely(!upipe_avfilt->configured))
        return;

    AVFrame *filt_frame = av_frame_alloc();
    if (unlikely(!filt_frame)) {
        upipe_err_va(upipe, "cannot allocate av frame");
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* pull filtered frames from the filtergraph */
    int err = av_buffersink_get_frame(upipe_avfilt_sub->buffer_ctx,
                                      filt_frame);
    if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
        av_frame_free(&filt_frame);
        return;
    }
    if (err < 0) {
        upipe_err_va(upipe, "cannot get frame from filter graph: %s",
                     av_err2str(err));
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        av_frame_free(&filt_frame);
        return;
    }
    upipe_avfilt_sub_output_frame(upipe, filt_frame);
    av_frame_unref(filt_frame);
    av_frame_free(&filt_frame);
}

/** @internal @This catches the internal events of the avfilter sub pipes.
 *
 * @param upipe description structure of the sub pipe
 * @param uprobe structure used to raise events
 * @param event event thrown
 * @param args optional arguments of the event
 * @return an error code
 */
static int upipe_avfilt_sub_catch(struct uprobe *uprobe,
                                  struct upipe *upipe,
                                  int event,
                                  va_list args)
{
    if (event == UPROBE_AVFILT_SUB_UPDATE &&
        ubase_get_signature(args) == UPIPE_AVFILT_SUB_SIGNATURE) {
        upipe_avfilt_sub_wait(upipe, 0);
        return UBASE_ERR_NONE;
    }
    else
        return uprobe_throw_next(uprobe, upipe, event, args);
}

/** @internal @This creates the filter of the sub pipe.
 *
 * @param upipe description structure of the sub pipe
 * @return an error code
 */
static int upipe_avfilt_sub_create_filter(struct upipe *upipe)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    if (unlikely(upipe_avfilt_sub->buffer_ctx))
        return UBASE_ERR_BUSY;

    if (unlikely(!upipe_avfilt->filter_graph))
        return UBASE_ERR_INVALID;

    const char *name = NULL;
    switch (upipe_avfilt_sub->media_type) {
        case UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO:
            name = "buffer";
            break;
        case UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO:
            name = "abuffer";
            break;
        case UPIPE_AVFILT_SUB_MEDIA_TYPE_UNKNOWN:
            upipe_err(upipe, "unknown media type");
            return UBASE_ERR_INVALID;
    }

    const AVFilter *filter = avfilter_get_by_name(name);
    if (filter == NULL) {
        upipe_err(upipe, "no buffer found for this media type");
        return UBASE_ERR_INVALID;
    }

    AVFilterContext *ctx = avfilter_graph_alloc_filter(
        upipe_avfilt->filter_graph, filter, upipe_avfilt_sub->name);
    if (ctx == NULL) {
        upipe_err_va(upipe, "cannot create %s filter", name);
        return UBASE_ERR_EXTERNAL;
    }

    if (upipe_avfilt_sub->input) {
        AVBufferSrcParameters *p = av_buffersrc_parameters_alloc();
        if (p == NULL) {
            upipe_err_va(upipe, "cannot alloc %s params", name);
            avfilter_free(ctx);
            return UBASE_ERR_EXTERNAL;
        }
        p->time_base.num = 1;
        p->time_base.den = UCLOCK_FREQ;
        switch (upipe_avfilt_sub->media_type) {
            case UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO:
                p->format = upipe_avfilt_sub->video.pix_fmt;
                p->width = upipe_avfilt_sub->video.width;
                p->height = upipe_avfilt_sub->video.height;
                if (upipe_avfilt_sub->video.sar.num) {
                    p->sample_aspect_ratio.num = upipe_avfilt_sub->video.sar.num;
                    p->sample_aspect_ratio.den = upipe_avfilt_sub->video.sar.den;
                }
                if (upipe_avfilt_sub->video.fps.num) {
                    p->frame_rate.num = upipe_avfilt_sub->video.fps.num;
                    p->frame_rate.den = upipe_avfilt_sub->video.fps.den;
                }
                break;
            case UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO:
                p->format = upipe_avfilt_sub->audio.sample_fmt;
                p->sample_rate = upipe_avfilt_sub->audio.sample_rate;
                p->channel_layout = upipe_avfilt_sub->audio.channel_layout;
                break;
            case UPIPE_AVFILT_SUB_MEDIA_TYPE_UNKNOWN:
                break;
        }

        int err = av_buffersrc_parameters_set(ctx, p);
        av_free(p);
        if (err < 0) {
            upipe_err_va(upipe, "cannot set %s params: %s",
                         name, av_err2str(err));
            avfilter_free(ctx);
            return UBASE_ERR_EXTERNAL;
        }
    }
    if (avfilter_init_dict(ctx, NULL)) {
        avfilter_free(ctx);
        return UBASE_ERR_EXTERNAL;
    }
    upipe_avfilt_sub->buffer_ctx = ctx;

    return UBASE_ERR_NONE;
}

/** @internal @This allocates and initializes a avfilter sub pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated and initialized sub pipe or NULL
 */
static struct upipe *upipe_avfilt_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe =
        upipe_avfilt_sub_alloc_flow(
            mgr, uprobe_alloc(upipe_avfilt_sub_catch, uprobe),
            signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;

    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    upipe_avfilt_sub_init_urefcount(upipe);
    upipe_avfilt_sub_init_sub(upipe);
    upipe_avfilt_sub_init_output(upipe);
    upipe_avfilt_sub_init_uclock(upipe);
    upipe_avfilt_sub_init_upump_mgr(upipe);
    upipe_avfilt_sub_init_upump(upipe);

    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    upipe_avfilt_sub->flow_def_alloc = flow_def;
    upipe_avfilt_sub->ubuf_mgr = ubuf_mgr_use(upipe_avfilt->ubuf_mgr);
    upipe_avfilt_sub->name = NULL;
    upipe_avfilt_sub->input = false;
    upipe_avfilt_sub->pts_prog_offset = 0;
    upipe_avfilt_sub->pts_sys_offset = UINT64_MAX;
    upipe_avfilt_sub->last_pts_prog = UINT64_MAX;
    upipe_avfilt_sub->last_duration = 0;
    upipe_avfilt_sub->buffer_ctx = NULL;
    upipe_avfilt_sub->warn_not_configured = true;
    upipe_avfilt_sub->media_type = UPIPE_AVFILT_SUB_MEDIA_TYPE_UNKNOWN;
    ulist_init(&upipe_avfilt_sub->urefs);

    upipe_throw_ready(upipe);

    int ret = uref_avfilt_flow_get_name(flow_def, &upipe_avfilt_sub->name);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "no avfilter name set");
        upipe_release(upipe);
        return NULL;
    }

    upipe_avfilt_reset(upipe_avfilt_to_upipe(upipe_avfilt));

    return upipe;
}

/** @internal @This is called when there is no more reference on the sub pipe.
 *
 * @param upipe description structure of the sub pipe
 */
static void upipe_avfilt_sub_free(struct upipe *upipe)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_avfilt_sub->urefs)))
        uref_free(uref_from_uchain(uchain));

    upipe_throw_dead(upipe);

    uref_free(upipe_avfilt_sub->flow_def_alloc);
    ubuf_mgr_release(upipe_avfilt_sub->ubuf_mgr);
    upipe_avfilt_sub_clean_upump(upipe);
    upipe_avfilt_sub_clean_upump_mgr(upipe);
    upipe_avfilt_sub_clean_uclock(upipe);
    upipe_avfilt_sub_clean_output(upipe);
    upipe_avfilt_sub_clean_sub(upipe);
    upipe_avfilt_sub_clean_urefcount(upipe);
    upipe_avfilt_clean_filters(upipe_avfilt_to_upipe(upipe_avfilt));
    upipe_avfilt_init_filters(upipe_avfilt_to_upipe(upipe_avfilt));
    upipe_avfilt_sub_free_flow(upipe);
}

/** @internal @This converts an uref pic to an avframe.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref to convert
 * @param frame filled with the convertion
 * @return an error code
 */
static int upipe_avfilt_sub_avframe_from_uref_pic(struct upipe *upipe,
                                                  struct uref *uref,
                                                  AVFrame *frame)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    size_t hsize, vsize;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 hsize != upipe_avfilt_sub->video.width ||
                 vsize != upipe_avfilt_sub->video.height))
        goto inval;

    for (int i = 0; i < UPIPE_AV_MAX_PLANES &&
         upipe_avfilt_sub->video.chroma_map[i] != NULL; i++) {
        const uint8_t *data;
        size_t stride;
        uint8_t vsub;
        if (unlikely(
                !ubase_check(
                    uref_pic_plane_read(uref,
                                        upipe_avfilt_sub->video.chroma_map[i],
                                        0, 0, -1, -1, &data)) ||
                !ubase_check(
                    uref_pic_plane_size(uref,
                                        upipe_avfilt_sub->video.chroma_map[i],
                                        &stride, NULL, &vsub, NULL))))
            goto inval;
        frame->data[i] = (uint8_t *)data;
        frame->linesize[i] = stride;
        frame->buf[i] = av_buffer_create(frame->data[i],
                                         stride * vsize / vsub,
                                         buffer_free_pic_cb, uref,
                                         AV_BUFFER_FLAG_READONLY);
        if (frame->buf[i] == NULL) {
            uref_pic_plane_unmap(uref, upipe_avfilt_sub->video.chroma_map[i],
                                 0, 0, -1, -1);
            goto inval;
        }

        /* use this as an avcodec refcount */
        uref_attr_set_priv(uref, i + 1);
    }

    frame->extended_data = frame->data;
    frame->width = hsize;
    frame->height = vsize;
    frame->key_frame = ubase_check(uref_pic_get_key(uref));
    frame->format = upipe_avfilt_sub->video.pix_fmt;
    frame->interlaced_frame = !ubase_check(uref_pic_get_progressive(uref));
    frame->top_field_first = ubase_check(uref_pic_get_tff(uref));

    uint64_t number;
    if (ubase_check(uref_pic_get_number(uref, &number)))
        frame->coded_picture_number = number;

    uint64_t pts = UINT64_MAX;
    if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
        frame->pts = pts;

    uint64_t duration = UINT64_MAX;
    if (ubase_check(uref_clock_get_duration(uref, &duration)))
        frame->pkt_duration = duration;

    upipe_verbose_va(upipe, " input frame %d(%d) %ix%i pts=%f duration=%f",
                     frame->display_picture_number,
                     frame->coded_picture_number,
                     frame->width, frame->height,
                     (double) pts / UCLOCK_FREQ,
                     (double) duration / UCLOCK_FREQ);

    return UBASE_ERR_NONE;

inval:
    upipe_warn(upipe, "invalid buffer received");
    uref_free(uref);
    return UBASE_ERR_INVALID;
}

/** @internal @This converts an uref sound to an avframe.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref to convert
 * @param frame filled with the convertion
 * @return an error code
 */
static int upipe_avfilt_sub_avframe_from_uref_sound(struct upipe *upipe,
                                                    struct uref *uref,
                                                    AVFrame *frame)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    size_t size;
    uint8_t sample_size;
    UBASE_RETURN(uref_sound_size(uref, &size, &sample_size));

    unsigned i = 0;
    const char *channel;
    uref_sound_foreach_plane(uref, channel) {
        const uint8_t *data;

        if (unlikely(!ubase_check(uref_sound_plane_read_uint8_t(
                        uref, channel, 0, -1, &data)))) {
            upipe_warn_va(upipe, "fail to read channel %s", channel);
            continue;
        }

        frame->data[i] = (uint8_t *)data;
        frame->linesize[i] = size * sample_size;
        frame->buf[i] = av_buffer_create(frame->data[i],
                                         size * sample_size,
                                         buffer_free_sound_cb, uref,
                                         AV_BUFFER_FLAG_READONLY);
        uref_attr_set_priv(uref, i + 1);
        i++;
    }

    uint64_t pts = UINT64_MAX;
    if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
        frame->pts = pts;

    uint64_t duration = UINT64_MAX;
    if (ubase_check(uref_clock_get_duration(uref, &duration)))
        frame->pkt_duration = duration;

    frame->extended_data = frame->data;
    frame->nb_samples = size;
    frame->format = upipe_avfilt_sub->audio.sample_fmt;
    frame->sample_rate = upipe_avfilt_sub->audio.sample_rate;
    frame->channel_layout = upipe_avfilt_sub->audio.channel_layout;
    frame->channels = upipe_avfilt_sub->audio.channels;

    upipe_verbose_va(upipe, " input frame pts=%f duration=%f",
                     (double) pts / UCLOCK_FREQ,
                     (double) duration / UCLOCK_FREQ);

    return UBASE_ERR_NONE;
}


/** @internal @This converts an uref to an avframe.
 *
 * @param upipe description structure of the pipe
 * @param uref input uref to convert
 * @param frame filled with the convertion
 * @return an error code
 */
static int upipe_avfilt_sub_avframe_from_uref(struct upipe *upipe,
                                             struct uref *uref,
                                             AVFrame *frame)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    if (ubase_check(uref_flow_match_def(upipe_avfilt_sub->flow_def,
                                        UREF_PIC_FLOW_DEF)))
        return upipe_avfilt_sub_avframe_from_uref_pic(upipe, uref, frame);
    else if (ubase_check(uref_flow_match_def(upipe_avfilt_sub->flow_def,
                                             UREF_SOUND_FLOW_DEF)))
        return upipe_avfilt_sub_avframe_from_uref_sound(upipe, uref, frame);

    const char *def = "(none)";
    uref_flow_get_def(upipe_avfilt_sub->flow_def, &def);
    upipe_warn_va(upipe, "unsupported flow def %s", def);
    return UBASE_ERR_INVALID;
}

/** @internal @This handles the input buffer.
 *
 * @param upipe description structure of the subpipe
 * @param uref input buffer to handle
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_avfilt_sub_input(struct upipe *upipe,
                                  struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    if (unlikely(!upipe_avfilt_sub->input)) {
        upipe_err(upipe, "receive buffer in an output sub pipe");
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_avfilt->configured)) {
        if (upipe_avfilt_sub->warn_not_configured)
            upipe_warn(upipe, "filter graph is not configured");
        upipe_avfilt_sub->warn_not_configured = false;
        uref_free(uref);
        return;
    }
    upipe_avfilt_sub->warn_not_configured = true;

    AVFrame *frame = av_frame_alloc();
    if (unlikely(!frame)) {
        upipe_err_va(upipe, "cannot allocate av frame");
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (!ubase_check(ubuf_av_get_avframe(uref->ubuf, frame))) {
        int ret = upipe_avfilt_sub_avframe_from_uref(upipe, uref, frame);
        if (unlikely(!ubase_check(ret))) {
            upipe_throw_error(upipe, ret);
            av_frame_free(&frame);
            return;
        }
    }
    else {
        uint64_t pts = UINT64_MAX;
        if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
            frame->pts = pts;

        uint64_t duration = UINT64_MAX;
        if (ubase_check(uref_clock_get_duration(uref, &duration)))
            frame->pkt_duration = duration;

        uref_free(uref);
    }

    int err = av_buffersrc_write_frame(upipe_avfilt_sub->buffer_ctx, frame);
    av_frame_free(&frame);
    if (unlikely(err < 0)) {
        upipe_err_va(upipe, "cannot write frame to filter graph: %s",
                     av_err2str(err));
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        return;
    }

    upipe_avfilt_update_outputs(upipe_avfilt_to_upipe(upipe_avfilt));
}

/** @internal @This sets the input sub pipe flow definition for video.
 *
 * @param upipe description structure of the sub pipe
 * @param flow_def new input flow defintion
 * @return an error code
 */
static int upipe_avfilt_sub_set_flow_def_pic(struct upipe *upipe,
                                             struct uref *flow_def)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    const char *chroma_map[UPIPE_AV_MAX_PLANES];
    enum AVPixelFormat pix_fmt;
    uint64_t width;
    uint64_t height;
    struct urational sar = { 1, 1 };
    struct urational fps = { 1, 1 };

    pix_fmt = upipe_av_pixfmt_from_flow_def(flow_def, NULL, chroma_map);
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &width));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &height));
    uref_pic_flow_get_sar(flow_def, &sar);
    uref_pic_flow_get_fps(flow_def, &fps);

    if (upipe_avfilt->filter_graph &&
        upipe_avfilt_sub->media_type == UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO &&
        upipe_avfilt_sub->video.pix_fmt == pix_fmt &&
        upipe_avfilt_sub->video.width == width &&
        upipe_avfilt_sub->video.height == height &&
        !urational_cmp(&upipe_avfilt_sub->video.sar, &sar) &&
        !urational_cmp(&upipe_avfilt_sub->video.fps, &fps))
        return UBASE_ERR_NONE;


    upipe_avfilt_sub->media_type = UPIPE_AVFILT_SUB_MEDIA_TYPE_VIDEO;
    memcpy(upipe_avfilt_sub->video.chroma_map, chroma_map, sizeof (chroma_map));
    upipe_avfilt_sub->video.pix_fmt = pix_fmt;
    upipe_avfilt_sub->video.width = width;
    upipe_avfilt_sub->video.height = height;
    upipe_avfilt_sub->video.sar = sar;
    upipe_avfilt_sub->video.fps = fps;

    upipe_avfilt_clean_filters(upipe_avfilt_to_upipe(upipe_avfilt));
    upipe_avfilt_init_filters(upipe_avfilt_to_upipe(upipe_avfilt));

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input sub pipe flow definition for audio.
 *
 * @param upipe description structure of the sub pipe
 * @param flow_def new input flow defintion
 * @return an error code
 */
static int upipe_avfilt_sub_set_flow_def_sound(struct upipe *upipe,
                                               struct uref *flow_def)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    uint8_t channels = 0;
    uint64_t channel_layout;
    uint64_t sample_rate;
    enum AVSampleFormat sample_fmt;

    sample_fmt = upipe_av_samplefmt_from_flow_def(flow_def, &channels);
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &sample_rate));
    switch (channels) {
        case 1:
            channel_layout = AV_CH_LAYOUT_MONO;
            break;
        case 2:
            channel_layout = AV_CH_LAYOUT_STEREO;
            break;
        case 5:
            channel_layout = AV_CH_LAYOUT_5POINT1_BACK;
            break;
        default:
            upipe_warn(upipe, "unsupported channel layout");
            return UBASE_ERR_INVALID;
    }

    if (upipe_avfilt->filter_graph &&
        upipe_avfilt_sub->media_type == UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO &&
        upipe_avfilt_sub->audio.sample_fmt == sample_fmt &&
        upipe_avfilt_sub->audio.channels == channels &&
        upipe_avfilt_sub->audio.channel_layout == channel_layout &&
        upipe_avfilt_sub->audio.sample_rate == sample_rate)
        return UBASE_ERR_NONE;

    upipe_avfilt_sub->media_type = UPIPE_AVFILT_SUB_MEDIA_TYPE_AUDIO;
    upipe_avfilt_sub->audio.sample_fmt = sample_fmt;
    upipe_avfilt_sub->audio.channels = channels;
    upipe_avfilt_sub->audio.channel_layout = channel_layout;
    upipe_avfilt_sub->audio.sample_rate = sample_rate;

    upipe_avfilt_clean_filters(upipe_avfilt_to_upipe(upipe_avfilt));
    upipe_avfilt_init_filters(upipe_avfilt_to_upipe(upipe_avfilt));

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input sub pipe flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new input flow defintion
 * @return an error code
 */
static int upipe_avfilt_sub_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    upipe_avfilt_sub->input = true;

    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_avfilt_sub_store_flow_def(upipe, flow_def_dup);

    if (ubase_check(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))) {
        UBASE_RETURN(upipe_avfilt_sub_set_flow_def_pic(upipe, flow_def));
    }
    else if (ubase_check(uref_flow_match_def(flow_def, UREF_SOUND_FLOW_DEF))) {
        UBASE_RETURN(upipe_avfilt_sub_set_flow_def_sound(upipe, flow_def));
    }
    else {
        const char *def = "(none)";
        uref_flow_get_def(flow_def, &def);
        upipe_warn_va(upipe, "unsupported flow def %s", def);
        upipe_avfilt_clean_filters(upipe_avfilt_to_upipe(upipe_avfilt));
        return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles the avfilter sub pipe control commands.
 *
 * @param upipe description structure of the sub pipe
 * @param command control command to handle
 * @param args optional arguments of the control command
 * @return an error code
 */
static int upipe_avfilt_sub_control_real(struct upipe *upipe, int cmd, va_list args)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);

    UBASE_HANDLED_RETURN(upipe_avfilt_sub_control_super(upipe, cmd, args));
    if (!upipe_avfilt_sub->input)
        UBASE_HANDLED_RETURN(upipe_avfilt_sub_control_output(upipe, cmd, args));

    switch (cmd) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_avfilt_sub_set_upump(upipe, NULL);
            return upipe_avfilt_sub_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_avfilt_sub_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avfilt_sub_set_flow_def(upipe, flow_def);
        }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This checks the internal state of the sub pipe.
 *
 * @param upipe description structure of the sub pipe
 * @return an error code
 */
static int upipe_avfilt_sub_check(struct upipe *upipe)
{
    struct upipe_avfilt_sub *upipe_avfilt_sub =
        upipe_avfilt_sub_from_upipe(upipe);
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_sub_mgr(upipe->mgr);

    if (!upipe_avfilt_sub->input)
        upipe_avfilt_sub_check_upump_mgr(upipe);

    if (!upipe_avfilt_sub->upump_mgr)
        return UBASE_ERR_NONE;

    if (upipe_avfilt->filter_graph)
        upipe_avfilt_sub_wait(upipe, 0);

    return UBASE_ERR_NONE;
}

/** @internal @This handles the avfilter sub pipe control commands and checks the
 * internal state.
 *
 * @param upipe description structure of the sub pipe
 * @param command control command to handle
 * @param args optional arguments of the control command
 * @return an error code
 */
static int upipe_avfilt_sub_control(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_RETURN(upipe_avfilt_sub_control_real(upipe, cmd, args));
    return upipe_avfilt_sub_check(upipe);
}

/** @internal @This sets the configured value and throws an event is the value
 * has changed..
 *
 * @param upipe description structure of the pipe
 * @param configured true if the filter is configured, false otherwise
 * @return an error code
 */
static int upipe_avfilt_set_configured(struct upipe *upipe, bool configured)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    if (upipe_avfilt->configured == configured)
        return UBASE_ERR_NONE;

    upipe_notice_va(upipe, "filter %s is %s",
                    upipe_avfilt->filters_desc ?: "(none)",
                    configured ? "configured" :
                    "not configured");
    upipe_avfilt->configured = configured;
    return upipe_throw(upipe, UPROBE_AVFILT_CONFIGURED, UPIPE_AVFILT_SIGNATURE,
                       configured ? 1 : 0);
}

/** @internal @This updates the outputs if needed.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_update_outputs(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
        if (!sub->input)
            upipe_avfilt_sub_throw_update(upipe_avfilt_sub_to_upipe(sub));
    }
}

/** @internal @This cleans the avfilter graph.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_clean_filters(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    if (!upipe_avfilt->filter_graph)
        return;

    struct uchain *uchain;
    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
        sub->buffer_ctx = NULL;
    }
    avfilter_graph_free(&upipe_avfilt->filter_graph);
    upipe_avfilt_set_configured(upipe, false);
}

/** @internal @This initializes the avfilter graph.
 * This must be called when all the sub pipes have been created.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_avfilt_init_filters(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    struct uchain *uchain;
    int ret = UBASE_ERR_EXTERNAL, err;
    AVFilterInOut *inputs = NULL;
    AVFilterInOut *outputs = NULL;

    if (!upipe_avfilt->filters_desc)
        return UBASE_ERR_NONE;

    if (upipe_avfilt->filter_graph)
        return UBASE_ERR_NONE;

    upipe_avfilt->filter_graph = avfilter_graph_alloc();
    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
        if (!sub->input)
            continue;

        ret = upipe_avfilt_sub_create_filter(upipe_avfilt_sub_to_upipe(sub));
        if (unlikely(!ubase_check(ret))) {
            upipe_err_va(upipe, "create filter for input %s failed", sub->name);
            goto end;
        }
    }

    ret = UBASE_ERR_EXTERNAL;

    AVFilterInOut *prev_output = NULL;
    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
        if (!sub->input)
            continue;

        AVFilterInOut *inout = avfilter_inout_alloc();
        if (unlikely(!inout)) {
            upipe_err(upipe, "cannot allocate inout");
            goto end;
        }
        inout->name = av_strdup(sub->name);
        inout->filter_ctx = sub->buffer_ctx;
        inout->pad_idx = 0;
        inout->next = NULL;
        if (prev_output)
            prev_output->next = inout;
        else
            outputs = inout;
        prev_output = inout;
    }

    upipe_dbg_va(upipe, "reconfiguring filter %s", upipe_avfilt->filters_desc);
    if ((err = avfilter_graph_parse_ptr(upipe_avfilt->filter_graph,
                                        upipe_avfilt->filters_desc,
                                        &inputs, &outputs,
                                        NULL)) < 0) {
        upipe_err_va(upipe, "cannot parse filter graph: %s",
                     av_err2str(err));
        goto end;
    }

    for (AVFilterInOut *input = inputs; input; input = input->next) {
        int type = avfilter_pad_get_type(input->filter_ctx->output_pads,
                                         input->pad_idx);
        upipe_dbg_va(upipe, "input \"%s\" type %s is not ready",
                     input->name, av_get_media_type_string(type));
        goto end;
    }

    char *filters = NULL;
    for (AVFilterInOut *output = outputs; output; output = output->next) {
        int type = avfilter_pad_get_type(output->filter_ctx->output_pads,
                                         output->pad_idx);
        upipe_dbg_va(upipe, "configure output \"%s\" type %s",
                     output->name, av_get_media_type_string(type));

        struct upipe_avfilt_sub *upipe_avfilt_sub = NULL;
        ulist_foreach(&upipe_avfilt->subs, uchain) {
            struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
            if (!sub->input && sub->name && output->name &&
                !strcmp(sub->name, output->name))
                upipe_avfilt_sub = sub;
        }
        if (!upipe_avfilt_sub) {
            upipe_notice_va(upipe, "output %s is not created yet",
                            output->name);
            free(filters);
            goto end;
        }

        const char *filter = NULL;
        switch (type) {
            case AVMEDIA_TYPE_VIDEO:
                filter = "buffersink";
                break;
            case AVMEDIA_TYPE_AUDIO:
                filter = "abuffersink";
                break;
            default:
                break;
        }

        char *tmp = NULL;
        if (asprintf(&tmp, "%s%s[%s]%s@%s", filters ?: "",
                     filters ? "; " : "",
                     output->name, filter, output->name) < 0) {
            upipe_err(upipe, "fail to allocate string");
            free(filters);
            goto end;
        }
        free(filters);
        filters = tmp;
    }

    err = avfilter_graph_parse_ptr(upipe_avfilt->filter_graph,
                                   filters, NULL, &outputs, NULL);
    free(filters);
    if (err < 0) {
        upipe_err_va(upipe, "cannot parse filter graph: %s",
                     av_err2str(err));
        goto end;
    }

    AVFilterGraph *graph = upipe_avfilt->filter_graph;
    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *sub = upipe_avfilt_sub_from_uchain(uchain);
        if (sub->input)
            continue;

        bool found = false;
        for (unsigned i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *f = graph->filters[i];
            const char *name = f->name;
            if (strncmp(name, "buffersink@", strlen("buffersink@")) &&
                strncmp(name, "abuffersink@", strlen("abuffersink@")))
                continue;
            name = strchr(name, '@');
            name++;
            if (!strcmp(sub->name, name)) {
                sub->buffer_ctx = f;
                found = true;
                break;
            }
        }
        if (!found) {
            upipe_err_va(upipe, "%s not found", sub->name);
            goto end;
        }
    }

    if ((err = avfilter_graph_config(upipe_avfilt->filter_graph, NULL)) < 0) {
        upipe_err_va(upipe, "cannot configure filter graph: %s",
                     av_err2str(err));
        goto end;
    }

    ret = UBASE_ERR_NONE;
    upipe_avfilt_set_configured(upipe, true);

    ulist_foreach(&upipe_avfilt->subs, uchain) {
        struct upipe_avfilt_sub *upipe_avfilt_sub =
            upipe_avfilt_sub_from_uchain(uchain);
        struct upipe *sub = upipe_avfilt_sub_to_upipe(upipe_avfilt_sub);
        upipe_avfilt_sub_throw_update(sub);
    }

end:
    if (!ubase_check(ret))
        upipe_avfilt_clean_filters(upipe);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

/** @internal @This reinitialises the filter.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_reset(struct upipe *upipe)
{
    upipe_avfilt_clean_filters(upipe);
    upipe_avfilt_init_filters(upipe);
}

/** @internal @This sets the filter graph description.
 *
 * @param upipe description structure of the pipe
 * @param filters_desc filter graph description
 * @return an error code
 */
static int _upipe_avfilt_set_filters_desc(struct upipe *upipe,
                                          const char *filters_desc)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    char *filters_desc_dup = strdup(filters_desc);
    UBASE_ALLOC_RETURN(filters_desc_dup);
    free(upipe_avfilt->filters_desc);
    upipe_avfilt->filters_desc = filters_desc_dup;
    upipe_avfilt_reset(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an avfilter pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avfilt_control(struct upipe *upipe,
                                int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_avfilt_control_subs(upipe, command, args))

    switch (command) {
        case UPIPE_AVFILT_SET_FILTERS_DESC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVFILT_SIGNATURE)
            const char *filters_desc = va_arg(args, const char *);
            return _upipe_avfilt_set_filters_desc(upipe, filters_desc);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This initializes the sub pipes manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    memset(&upipe_avfilt->sub_mgr, 0, sizeof (upipe_avfilt->sub_mgr));
    upipe_avfilt->sub_mgr.signature = UPIPE_AVFILT_SUB_SIGNATURE;
    upipe_avfilt->sub_mgr.refcount = upipe_avfilt_to_urefcount(upipe_avfilt);
    upipe_avfilt->sub_mgr.upipe_alloc = upipe_avfilt_sub_alloc;
    upipe_avfilt->sub_mgr.upipe_input = upipe_avfilt_sub_input;
    upipe_avfilt->sub_mgr.upipe_control = upipe_avfilt_sub_control;
}

/** @internal @This allocates an avfilter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfilt_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_avfilt_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_avfilt_init_urefcount(upipe);
    upipe_avfilt_init_sub_mgr(upipe);
    upipe_avfilt_init_sub_subs(upipe);

    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);
    upipe_avfilt->filters_desc = NULL;
    upipe_avfilt->filter_graph = NULL;
    upipe_avfilt->configured = false;
    upipe_avfilt->ubuf_mgr = ubuf_av_mgr_alloc();

    upipe_throw_ready(upipe);

    if (unlikely(!upipe_avfilt->ubuf_mgr)) {
        upipe_release(upipe);
        return NULL;
    }

    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfilt_free(struct upipe *upipe)
{
    struct upipe_avfilt *upipe_avfilt = upipe_avfilt_from_upipe(upipe);

    upipe_throw_dead(upipe);

    free(upipe_avfilt->filters_desc);
    avfilter_graph_free(&upipe_avfilt->filter_graph);
    ubuf_mgr_release(upipe_avfilt->ubuf_mgr);
    upipe_avfilt_clean_sub_subs(upipe);
    upipe_avfilt_clean_urefcount(upipe);
    upipe_avfilt_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfilt_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AVFILT_SIGNATURE,

    .upipe_alloc = upipe_avfilt_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_avfilt_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for avfilter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfilt_mgr_alloc(void)
{
    return &upipe_avfilt_mgr;
}
