/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe avcodec decode module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-modules/upipe_proxy.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <upipe-av/upipe_av_pixfmt.h>
#include <upipe-av/upipe_av_samplefmt.h>
#include "upipe_av_internal.h"

#define EXPECTED_FLOW "block."

/** @hidden */
static bool upipe_avcdec_decode(struct upipe *upipe, struct uref *uref,
                                struct upump *upump);

/** upipe_avcdec structure with avcdec parameters */ 
struct upipe_avcdec {
    /** input flow */
    struct uref *input_flow;
    /** output flow */
    struct uref *output_flow;
    /** true if the flow definition has already been sent */
    bool output_flow_sent;
    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** upump mgr */
    struct upump_mgr *upump_mgr;

    /** upipe/av pixfmt translator */
    const struct upipe_av_pixfmt *pixfmt;

    /** avcodec_open watcher */
    struct upump *upump_av_deal;
    /** temporary uref storage (used during udeal) */
    struct ulist urefs;
    /** list of blockers (used during udeal) */
    struct ulist blockers;

    /** frame counter */
    uint64_t counter;
    /** rap offset */
    uint8_t index_rap;
    /** previous rap */
    uint64_t prev_rap;
    /** latest incoming uref */
    struct uref *uref;
    /** next PTS */
    uint64_t next_pts;
    /** next PTS (systime time) */
    uint64_t next_pts_sys;

    /* parameters for last advertised outflow */
    /** last sample format */
    enum AVSampleFormat sample_fmt;
    /** last number of channels */
    int channels;
    /** last sample rate */
    int sample_rate;
    /** last frame size */
    int frame_size;
    /** last pixel format */
    enum PixelFormat pixel_fmt;
    /** last time base */
    AVRational time_base;

    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** true if the context will be closed */
    bool close;

    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_avcdec, upipe, UPIPE_AVCDEC_SIGNATURE);
UPIPE_HELPER_FLOW(upipe_avcdec, EXPECTED_FLOW)
UPIPE_HELPER_OUTPUT(upipe_avcdec, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avcdec, ubuf_mgr);
UPIPE_HELPER_UPUMP_MGR(upipe_avcdec, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcdec, upump_av_deal, upump_mgr)
UPIPE_HELPER_SINK(upipe_avcdec, urefs, blockers, upipe_avcdec_decode)

/* Documentation from libavcodec.h (get_buffer) :
 * The function will set AVFrame.data[], AVFrame.linesize[].
 * AVFrame.extended_data[] must also be set, but it should be the same as
 * AVFrame.data[] except for planar audio with more channels than can fit
 * in AVFrame.data[].  In that case, AVFrame.data[] shall still contain as
 * many data pointers as it can hold.  if CODEC_CAP_DR1 is not set then
 * get_buffer() must call avcodec_default_get_buffer() instead of providing
 * buffers allocated by some other means.
 * 
 * AVFrame.data[] should be 32- or 16-byte-aligned unless the CPU doesn't
 * need it.  avcodec_default_get_buffer() aligns the output buffer
 * properly, but if get_buffer() is overridden then alignment
 * considerations should be taken into account.
 * 
 * If pic.reference is set then the frame will be read later by libavcodec.
 * avcodec_align_dimensions2() should be used to find the required width
 * and height, as they normally need to be rounded up to the next multiple
 * of 16.
 * 
 * If frame multithreading is used and thread_safe_callbacks is set, it may
 * be called from a different thread, but not from more than one at once.
 * Does not need to be reentrant.
 */

/** @internal @This is called by avcodec when allocating a new picture.
 *
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer_pic(struct AVCodecContext *context,
                                       AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct ubuf *ubuf_pic;
    int width_aligned, height_aligned, i;
    const struct upipe_av_plane *planes = NULL;
    size_t stride = 0;

    frame->opaque = upipe_avcdec->uref;
    upipe_avcdec->uref = NULL;
    if (!frame->opaque) { /* dont die on invalid (?) calls */
        return -1;
    }

    uint64_t framenum = 0;
    uref_pic_get_number(frame->opaque, &framenum);

    upipe_verbose_va(upipe, "Allocating frame for %"PRIu64" (%p) - %dx%d",
                 framenum, frame->opaque, frame->width, frame->height);

    if (unlikely(!upipe_avcdec->pixfmt)) {
        upipe_avcdec->pixfmt = upipe_av_pixfmt_from_ubuf_mgr(upipe_avcdec->ubuf_mgr);
        if (unlikely(!upipe_avcdec->pixfmt)) {
            upipe_err_va(upipe, "frame format of ubuf manager not recognized");
            return -1;
        }
    }
    if (context->pix_fmt != *upipe_avcdec->pixfmt->pixfmt) {
        upipe_err_va(upipe, "frame format not compatible (%s != %s",
                                       av_get_pix_fmt_name(context->pix_fmt),
                            av_get_pix_fmt_name(*upipe_avcdec->pixfmt->pixfmt));
        return -1;
    }
    planes = upipe_avcdec->pixfmt->planes;

    /* direct rendering - allocate ubuf pic */
    if (upipe_avcdec->context->codec->capabilities & CODEC_CAP_DR1) {
        width_aligned = context->width;
        height_aligned = context->height;

        /* use avcodec width/height alignement, then resize pic */
        avcodec_align_dimensions(context, &width_aligned, &height_aligned);
        ubuf_pic = ubuf_pic_alloc(upipe_avcdec->ubuf_mgr, width_aligned, height_aligned);

        if (likely(ubuf_pic)) {
            ubuf_pic_resize(ubuf_pic, 0, 0, context->width, context->height);
            uref_attach_ubuf(frame->opaque, ubuf_pic);

            for (i=0; i < 4 && planes[i].chroma; i++) {
                ubuf_pic_plane_write(ubuf_pic, planes[i].chroma,
                        0, 0, -1, -1, &frame->data[i]);
                ubuf_pic_plane_size(ubuf_pic, planes[i].chroma, &stride,
                        NULL, NULL, NULL);
                frame->linesize[i] = stride;
            }

            frame->extended_data = frame->data;
            frame->type = FF_BUFFER_TYPE_USER;
            
            return 0; /* success */
        } else {
            upipe_verbose_va(upipe, "ubuf_pic_alloc(%d, %d) failed, fallback", width_aligned, height_aligned);
        }
    }

    /* default : DR failed or not available */
    return avcodec_default_get_buffer(context, frame);
}

/** @internal @This is called by avcodec when releasing a picture.
 *
 * @param context current avcodec context
 * @param frame avframe handler released by avcodec black magic box
 */
static void upipe_avcdec_release_buffer_pic(struct AVCodecContext *context,
                                            AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct uref *uref = frame->opaque;
    const struct upipe_av_plane *planes = NULL;
    int i;

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_verbose_va(upipe, "Releasing frame %"PRIu64" (%p)", (uint64_t) framenum, uref);

    if (likely(uref->ubuf)) {
        planes = upipe_avcdec_from_upipe(upipe)->pixfmt->planes;
        for (i=0; i < 4 && planes[i].chroma; i++) {
            ubuf_pic_plane_unmap(uref->ubuf, planes[i].chroma, 0, 0, -1, -1);
            frame->data[i] = NULL;
        }
    } else {
        avcodec_default_release_buffer(context, frame);
    }
    uref_free(uref);
}

/** @internal @This is called by avcodec when allocating a new audio buffer.
 * Used with audio decoders.
 *
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer_sound(struct AVCodecContext *context,
                                         AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct ubuf *ubuf_samples;
    uint8_t *buf;
    int size;
    uint64_t framenum = 0;

    frame->opaque = upipe_avcdec->uref;
    upipe_avcdec->uref = NULL;
    if (!frame->opaque) { /* dont die on invalid (?) calls */
        return -1;
    }

    uref_pic_get_number(frame->opaque, &framenum);
    upipe_verbose_va(upipe, "Allocating frame for %"PRIu64" (%p)",
                     framenum, frame->opaque);


    /* direct rendering - allocate ubuf for audio */
    if (!av_sample_fmt_is_planar(upipe_avcdec->context->sample_fmt) &&
        (upipe_avcdec->context->codec->capabilities & CODEC_CAP_DR1)) {
        ubuf_samples = ubuf_block_alloc(upipe_avcdec->ubuf_mgr,
                    av_samples_get_buffer_size(NULL, context->channels,
                        frame->nb_samples, context->sample_fmt, 1));

        if (likely(ubuf_samples)) {
            ubuf_block_write(ubuf_samples, 0, &size, &buf);
            uref_attach_ubuf(frame->opaque, ubuf_samples);

            av_samples_fill_arrays(frame->data, frame->linesize, buf,
                    context->channels, frame->nb_samples, context->sample_fmt, 1);

            frame->extended_data = frame->data;
            frame->type = FF_BUFFER_TYPE_USER;
            
            return 0; /* success */
        } else {
            upipe_verbose_va(upipe, "ubuf allocation failed, fallback");
        }
    }

    /* default : DR failed or not available */
    return avcodec_default_get_buffer(context, frame);
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (unlikely(upipe_avcdec->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avcdec->upump_av_deal);
        upump_free(upipe_avcdec->upump_av_deal);
        upipe_avcdec->upump_av_deal = NULL;
    }
}

/** @internal @This actually calls avcodec_open(). It may only be called by
 * one thread at a time.
 *
 * @param upipe description structure of the pipe
 * @return false if the buffers mustn't be dequeued
 */
static bool upipe_avcdec_do_av_deal(struct upipe *upipe)
{
    assert(upipe);
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;

    if (upipe_avcdec->close) {
        upipe_notice_va(upipe, "codec %s (%s) %d closed", context->codec->name, 
                        context->codec->long_name, context->codec->id);
        avcodec_close(context);
        /* trigger deferred release of the pipe */
        upipe_release(upipe);
        return false;
    }

    switch (context->codec->type) {
        case AVMEDIA_TYPE_VIDEO:
            context->get_buffer = upipe_avcdec_get_buffer_pic;
            context->release_buffer = upipe_avcdec_release_buffer_pic;
            /* otherwise we need specific prepend/append/align */
            context->flags |= CODEC_FLAG_EMU_EDGE;
            break;
        case AVMEDIA_TYPE_AUDIO:
            context->get_buffer = upipe_avcdec_get_buffer_sound;
            context->release_buffer = NULL;
            /* release_buffer is not called for audio */
            break;
        default:
            /* This should not happen */
            upipe_err_va(upipe, "Unsupported media type (%d)",
                         context->codec->type);
            return false;
    }

    /* open new context */
    if (unlikely(avcodec_open2(context, context->codec, NULL) < 0)) {
        upipe_warn(upipe, "could not open codec");
        upipe_throw_fatal(upipe, UPROBE_ERR_EXTERNAL);
        return false;
    }
    upipe_notice_va(upipe, "codec %s (%s) %d opened", context->codec->name, 
                    context->codec->long_name, context->codec->id);

    return true;
}

/** @internal @This is called to try an exclusive access on avcodec_open() or
 * avcodec_close().
 *
 * @param upump description structure of the pump
 */
static void upipe_avcdec_cb_av_deal(struct upump *upump)
{
    assert(upump);
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    /* check udeal */
    if (unlikely(!upipe_av_deal_grab()))
        return;

    /* avoid having the pipe disappear on us */
    upipe_use(upipe);

    /* real open_codec function */
    bool ret = upipe_avcdec_do_av_deal(upipe);

    /* clean dealer */
    upipe_av_deal_yield(upump);
    upump_free(upipe_avcdec->upump_av_deal);
    upipe_avcdec->upump_av_deal = NULL;

    if (ret) {
        upipe_avcdec_unblock_sink(upipe);
        upipe_avcdec_output_sink(upipe);
    } else
        upipe_avcdec_flush_sink(upipe);

    upipe_release(upipe);
}

/** @internal @This is called to trigger avcodec_open() or avcodec_close().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_start_av_deal(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    /* abort a pending open request */
    upipe_avcdec_abort_av_deal(upipe);
 
    /* use udeal/upump callback if available */
    if (upipe_avcdec->upump_mgr == NULL) {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcdec_open");
        upipe_avcdec_do_av_deal(upipe);
        return;
    }

    upipe_dbg(upipe, "upump_mgr present, using udeal");
    struct upump *upump_av_deal =
        upipe_av_deal_upump_alloc(upipe_avcdec->upump_mgr,
                                  upipe_avcdec_cb_av_deal, upipe);
    if (unlikely(!upump_av_deal)) {
        upipe_err(upipe, "can't create dealer");
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
        return;
    }
    upipe_avcdec->upump_av_deal = upump_av_deal;
    upipe_av_deal_start(upump_av_deal);
}

/** @internal @This is called to trigger avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_open(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    upipe_avcdec->close = false;
    upipe_avcdec_start_av_deal(upipe);
}

/** @internal @This is called to trigger avcodec_close().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_close(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    upipe_avcdec->close = true;
    upipe_avcdec_start_av_deal(upipe);
}

/** @internal @This sets the various time attributes.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false in case of allocation error
 */
static bool upipe_avcdec_set_time_attributes(struct upipe *upipe,
                                             struct uref *uref)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    uint64_t rap = 0, duration, pts, pts_sys;
    bool ret = true;

    /* rap */
    uref_clock_get_systime_rap(uref, &rap);
    if (unlikely(rap != upipe_avcdec->prev_rap)) {
        upipe_avcdec->prev_rap = rap;
        upipe_avcdec->index_rap = 0;
    }
    ret = ret && uref_clock_set_index_rap(uref, upipe_avcdec->index_rap);
    upipe_avcdec->index_rap++;

    /* pts */
    if (!uref_clock_get_pts(uref, &pts)) {
        pts = upipe_avcdec->next_pts;
        if (pts != UINT64_MAX) {
            ret = ret && uref_clock_set_pts(uref, pts);
        }
    }
    if (!uref_clock_get_pts_sys(uref, &pts_sys)) {
        pts_sys = upipe_avcdec->next_pts_sys;
        if (pts_sys != UINT64_MAX) {
            ret = ret && uref_clock_set_pts_sys(uref, pts_sys);
        }
    }

    /* DTS has no meaning from now on and is identical to PTS. */
    if (pts != UINT64_MAX)
        ret = ret && uref_clock_set_dts(uref, pts);
    else
        uref_clock_delete_dts(uref);
    if (pts_sys != UINT64_MAX)
        ret = ret && uref_clock_set_dts_sys(uref, pts_sys);
    else
        uref_clock_delete_dts_sys(uref);

    /* VBV demay has no meaning from now on. */
    ret = ret && uref_clock_delete_vbv_delay(uref);

    /* compute next pts based on current frame duration */
    if (pts != UINT64_MAX && uref_clock_get_duration(uref, &duration)) {
        upipe_avcdec->next_pts = pts + duration;
        if (pts_sys != UINT64_MAX)
            upipe_avcdec->next_pts_sys = pts_sys + duration;
    } else {
        upipe_warn(upipe, "couldn't determine next_pts");
    }
    return ret;
}

/** @internal @This outputs video frames.
 *
 * @param upipe description structure of the pipe
 * @param frame AVFrame structure
 * @param upump upump structure
 */
static void upipe_avcdec_output_pic(struct upipe *upipe, struct upump *upump)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;
    AVFrame *frame = upipe_avcdec->frame;
    struct uref *uref = uref_dup(frame->opaque);

    uint64_t framenum = 0;
    uref_pic_get_number(frame->opaque, &framenum);

    upipe_verbose_va(upipe, "%"PRIu64"\t - Picture decoded ! %dx%d - %"PRIu64,
                 upipe_avcdec->counter, frame->width, frame->height, framenum);

    /* if uref has no attached ubuf (ie DR not supported) */
    if (unlikely(!uref->ubuf)) {
        struct ubuf *ubuf = ubuf_pic_alloc(upipe_avcdec->ubuf_mgr,
                                           frame->width, frame->height);
        if (!ubuf) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        /* iterate through planes and copy data */
        const struct upipe_av_plane *planes = upipe_avcdec->pixfmt->planes;
        uint8_t *data, *src, hsub, vsub;
        const char *chroma = NULL; 
        size_t sstride, dstride;
        int i, j;

        i = j = 0;
        for (i=0; i < 4 && (chroma = planes[i].chroma); i++) {
            ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &data);
            ubuf_pic_plane_size(ubuf, chroma, &dstride, &hsub, &vsub, NULL);
            src = frame->data[i];
            sstride = frame->linesize[i];
            for (j = 0; j < frame->height/vsub; j++) {
                memcpy(data, src, frame->width/hsub);
                data += dstride;
                src += sstride;
            }
            ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1);
        }

        uref_attach_ubuf(uref, ubuf);
    }

    bool ret = true;
    ret = ret && uref_pic_set_tf(uref) && uref_pic_set_bf(uref);
    if (!frame->interlaced_frame)
        ret = ret && uref_pic_set_progressive(uref);
    else if (frame->top_field_first)
        ret = ret && uref_pic_set_tff(uref);

    /* set aspect-ratio */
    struct urational aspect;
    aspect.den = 0; /* null denom is invalid */
    if (frame->sample_aspect_ratio.num) {
        aspect.num = frame->sample_aspect_ratio.num;
        aspect.den = frame->sample_aspect_ratio.den;
        urational_simplify(&aspect);
        ret = ret && uref_pic_set_aspect(uref, aspect);
    }

    if (context->time_base.den)
        ret = ret && uref_clock_set_duration(uref,
                (uint64_t)(2 + frame->repeat_pict) * context->ticks_per_frame *
                UCLOCK_FREQ * context->time_base.num /
                (2 * context->time_base.den));

    /* various time-related attribute */
    ret = ret && upipe_avcdec_set_time_attributes(upipe, uref);
    if (!ret) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    /* flow definition */
    if (unlikely(upipe_avcdec->output_flow == NULL ||
                 upipe_avcdec->pixel_fmt != context->pix_fmt ||
                 memcmp(&upipe_avcdec->time_base, &context->time_base,
                        sizeof(AVRational)))) {
        struct uref *outflow = uref_dup(upipe_avcdec->input_flow);
        ret = ret && uref_flow_set_def(outflow, UREF_PIC_FLOW_DEF);
        ret = ret && uref_pic_flow_set_macropixel(outflow, 1);
        ret = ret && uref_pic_flow_set_planes(outflow, 0);
        int i;
        for (i = 0; i < 4; i++) {
            const struct upipe_av_plane *plane =
                &upipe_avcdec->pixfmt->planes[i];
            if (plane->chroma == NULL)
                break;
            ret = ret && uref_pic_flow_add_plane(outflow,
                                                 plane->hsub, plane->vsub,
                                                 plane->macropixel_size,
                                                 plane->chroma);
        }

        if (context->time_base.den) {
            struct urational fps = {
                .num = context->time_base.den,
                .den = context->time_base.num * context->ticks_per_frame
            };
            urational_simplify(&fps);
            ret = ret && uref_pic_flow_set_fps(outflow, fps);
        }
        if (context->sample_aspect_ratio.num) {
            struct urational sar = {
                .num = context->sample_aspect_ratio.num,
                .den = context->sample_aspect_ratio.den
            };
            urational_simplify(&sar);
            ret = ret && uref_pic_set_aspect(outflow, aspect);
        }
        if (!ret) {
            uref_free(outflow);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        upipe_avcdec_store_flow_def(upipe, outflow);
        upipe_avcdec->pixel_fmt = context->pix_fmt;
        upipe_avcdec->time_base = context->time_base;
    }

    upipe_avcdec_output(upipe, uref, upump);
}

/** @internal @This is a temporary function to interleave planar formats.
 *
 * @param upipe description structure of the pipe
 * @param buf output buffer
 * @param bufsize output buffer size
 */
static void upipe_avcdec_interleave(struct upipe *upipe, uint8_t *buf,
                                    int bufsize)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;
    AVFrame *frame = upipe_avcdec->frame;
    int sample_size = av_get_bytes_per_sample(context->sample_fmt);
    int channels = context->channels;
    unsigned int i;

    for (i = 0; i < frame->nb_samples; i++) {
        unsigned int j;
        for (j = 0; j < channels; j++) {
            unsigned int k;
            for (k = 0; k < sample_size; k++)
                *buf++ = frame->extended_data[j][i * sample_size + k];
        }
    }
}

/** @internal @This outputs audio buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump upump structure
 */
static void upipe_avcdec_output_sound(struct upipe *upipe, struct upump *upump)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;
    AVFrame *frame = upipe_avcdec->frame;
    struct uref *uref = frame->opaque;

    uint64_t framenum = 0;
    uref_pic_get_number(frame->opaque, &framenum);

    upipe_verbose_va(upipe, "%"PRIu64"\t - Frame decoded ! %"PRIu64,
                     upipe_avcdec->counter, framenum);

    /* fetch audio sample size (in case it has been reduced) */
    int avbufsize = av_samples_get_buffer_size(NULL, context->channels,
                       frame->nb_samples, context->sample_fmt, 1);
    enum AVSampleFormat sample_fmt = context->sample_fmt;

    /* if uref has no attached ubuf (ie DR not supported) */
    if (unlikely(!uref->ubuf)) {
        struct ubuf *ubuf = ubuf_block_alloc(upipe_avcdec->ubuf_mgr, avbufsize);
        if (unlikely(!ubuf)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        int bufsize = -1;
        uint8_t *buf;
        ubuf_block_write(ubuf, 0, &bufsize, &buf);
        if (av_sample_fmt_is_planar(sample_fmt)) {
            /* FIXME support planar formats */
            sample_fmt = av_get_packed_sample_fmt(sample_fmt);
            upipe_avcdec_interleave(upipe, buf, bufsize);
        } else
            memcpy(buf, frame->data[0], bufsize);
        ubuf_block_unmap(ubuf, 0);

        uref_attach_ubuf(uref, ubuf);
    } else {
        /* unmap, reduce block if needed */
        uref_block_unmap(uref, 0);
        uref_block_resize(uref, 0, avbufsize);
    }

    bool ret = true;
    /* samples in uref */
    ret = ret && uref_sound_flow_set_samples(uref, frame->nb_samples);
    if (context->sample_rate)
        ret = ret && uref_clock_set_duration(uref,
                                (uint64_t)frame->nb_samples * UCLOCK_FREQ /
                                context->sample_rate);

    /* various time-related attribute */
    ret = ret && upipe_avcdec_set_time_attributes(upipe, uref);
    if (!ret) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    /* flow definition */
    if (unlikely(upipe_avcdec->output_flow == NULL ||
                 upipe_avcdec->sample_fmt != sample_fmt ||
                 upipe_avcdec->channels != context->channels ||
                 upipe_avcdec->sample_rate != context->sample_rate ||
                 upipe_avcdec->frame_size != context->frame_size)) {
        struct uref *outflow = uref_dup(upipe_avcdec->input_flow);
        const char *def = upipe_av_samplefmt_to_flow_def(sample_fmt);
        ret = def != NULL;
        ret = ret && uref_flow_set_def(outflow, def);
        ret = ret && uref_sound_flow_set_channels(outflow, context->channels);
        ret = ret && uref_sound_flow_set_rate(outflow, context->sample_rate);
        if (context->frame_size)
            ret = ret && uref_sound_flow_set_samples(uref, context->frame_size);
        ret = ret && uref_sound_flow_set_sample_size(outflow,
                                 av_get_bytes_per_sample(context->sample_fmt));
        if (!ret) {
            uref_free(outflow);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }
        upipe_avcdec_store_flow_def(upipe, outflow);
        upipe_avcdec->sample_fmt = sample_fmt;
        upipe_avcdec->channels = context->channels;
        upipe_avcdec->sample_rate = context->sample_rate;
        upipe_avcdec->frame_size = context->frame_size;
    }

    upipe_avcdec_output(upipe, uref, upump);
}

/** @internal @This decodes packets.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 * @return always true
 */
static bool upipe_avcdec_decode(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    assert(upipe);
    assert(uref);

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVPacket avpkt;
    memset(&avpkt, 0, sizeof(AVPacket));
    av_init_packet(&avpkt);

    /* avcodec input buffer needs to be at least 4-byte aligned and
       FF_INPUT_BUFFER_PADDING_SIZE larger than actual input size.
       Thus, extract ubuf content in a properly allocated buffer.
       Padding must be zeroed. */
    size_t size = 0;
    uref_block_size(uref, &size);
    if (unlikely(!size)) {
        upipe_warn(upipe, "Received packet with size 0, dropping");
        uref_free(uref);
        return true;
    }
    avpkt.size = size;

    upipe_verbose_va(upipe, "Received packet %"PRIu64" - size : %zu",
                     upipe_avcdec->counter, avpkt.size);
    /* TODO replace with umem */
    avpkt.data = malloc(avpkt.size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(avpkt.data == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return true;
    }
    uref_block_extract(uref, 0, avpkt.size, avpkt.data); 
    ubuf_free(uref_detach_ubuf(uref));
    memset(avpkt.data + avpkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    uref_pic_set_number(uref, upipe_avcdec->counter++);

    /* Track current uref in pipe structure - required for buffer allocation
     * in upipe_avcdec_get_buffer */
    upipe_avcdec->uref = uref;

    int gotframe = 0, len;
    switch (upipe_avcdec->context->codec->type)
        case AVMEDIA_TYPE_VIDEO: {
            len = avcodec_decode_video2(upipe_avcdec->context,
                                        upipe_avcdec->frame,
                                        &gotframe, &avpkt);
            if (len < 0) {
                upipe_warn(upipe, "Error while decoding frame");
            }

            /* output frame if any has been decoded */
            if (gotframe) {
                upipe_avcdec_output_pic(upipe, upump);
            }
            break;

        case AVMEDIA_TYPE_AUDIO:
            len = avcodec_decode_audio4(upipe_avcdec->context,
                                        upipe_avcdec->frame,
                                        &gotframe, &avpkt);
            if (len < 0) {
                upipe_warn(upipe, "Error while decoding frame");
            }

            /* output samples if any has been decoded */
            if (gotframe) {
                upipe_avcdec_output_sound(upipe, upump);
            }
            break;

        default: {
            /* should never be here */
            upipe_err_va(upipe, "Unsupported media type (%d)",
                         upipe_avcdec->context->codec->type);
            break;
        }
    }

    free(avpkt.data);
    return true;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_avcdec_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    while (unlikely(!avcodec_is_open(upipe_avcdec->context))) {
        if (upipe_avcdec->upump_av_deal != NULL) {
            upipe_avcdec_block_sink(upipe, upump);
            upipe_avcdec_hold_sink(upipe, uref);
            return;
        }

        upipe_avcdec_open(upipe);
    }

    if (unlikely(uref->ubuf == NULL)) {
        if (unlikely(upipe_avcdec->output_flow == NULL)) {
            upipe_warn(upipe, "received empty uref before opening the codec");
            uref_free(uref);
        } else
            upipe_avcdec_output(upipe, uref, upump);
        return;
    }

    upipe_avcdec_decode(upipe, uref, upump);
}

/** @internal @This sets the content of an avcodec option. It only take effect
 * after the next call to @ref upipe_avcdec_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool _upipe_avcdec_set_option(struct upipe *upipe, const char *option,
                                     const char *content)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (avcodec_is_open(upipe_avcdec->context))
        return false;
    assert(option != NULL);
    int error = av_opt_set(upipe_avcdec->context, option, content,
                           AV_OPT_SEARCH_CHILDREN);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
        return false;
    }
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
static bool upipe_avcdec_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        /* generic linear stuff */
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_avcdec_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_avcdec_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_avcdec_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avcdec_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_avcdec_set_output(upipe, output);
        }
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_avcdec_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_avcdec_set_upump_av_deal(upipe, NULL);
            upipe_avcdec_abort_av_deal(upipe);
            return upipe_avcdec_set_upump_mgr(upipe, upump_mgr);
        }

        case UPIPE_AVCDEC_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDEC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avcdec_set_option(upipe, option, content);
        }

        default:
            return false;
    }
}

/** @This frees a upipe. We can only arrive here if the context has been
 * previously closed by releasing the proxy.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_free(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_avcdec->context->extradata);
    av_free(upipe_avcdec->context);
    av_free(upipe_avcdec->frame);
    uref_free(upipe_avcdec->input_flow);

    upipe_avcdec_abort_av_deal(upipe);
    upipe_avcdec_clean_sink(upipe);
    upipe_avcdec_clean_output(upipe);
    upipe_avcdec_clean_ubuf_mgr(upipe);
    upipe_avcdec_clean_upump_av_deal(upipe);
    upipe_avcdec_clean_upump_mgr(upipe);
    upipe_avcdec_free_flow(upipe);
}

/** @internal @This allocates a avcdec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avcdec_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_avcdec_alloc_flow(mgr, uprobe, signature,
                                                  args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint8_t *extradata_alloc = NULL;
    const uint8_t *extradata;
    size_t extradata_size = 0;
    if (uref_flow_get_headers(flow_def, &extradata, &extradata_size)) {
        extradata_alloc = malloc(extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (unlikely(extradata_alloc == NULL)) {
            uref_free(flow_def);
            upipe_avcdec_free_flow(upipe);
            return NULL;
        }
        memcpy(extradata_alloc, extradata, extradata_size);
        memset(extradata_alloc + extradata_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);
    }

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (unlikely((upipe_avcdec->frame = avcodec_alloc_frame()) == NULL)) {
        free(extradata_alloc);
        uref_free(flow_def);
        upipe_avcdec_free_flow(upipe);
        return NULL;
    }

    const char *def;
    enum AVCodecID codec_id;
    AVCodec *codec;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 !(codec_id =
                     upipe_av_from_flow_def(def + strlen(EXPECTED_FLOW))) ||
                 (codec = avcodec_find_decoder(codec_id)) == NULL ||
                 (upipe_avcdec->context =
                     avcodec_alloc_context3(codec)) == NULL)) {
        free(extradata_alloc);
        uref_free(flow_def);
        av_free(upipe_avcdec->frame);
        upipe_avcdec_free_flow(upipe);
        return NULL;
    }

    upipe_avcdec->context->codec = codec;
    upipe_avcdec->context->opaque = upipe;
    if (extradata_alloc != NULL) {
        upipe_avcdec->context->extradata = extradata_alloc;
        upipe_avcdec->context->extradata_size = extradata_size;
    }

    upipe_avcdec_init_ubuf_mgr(upipe);
    upipe_avcdec_init_upump_mgr(upipe);
    upipe_avcdec_init_upump_av_deal(upipe);
    upipe_avcdec_init_output(upipe);
    upipe_avcdec_init_sink(upipe);
    upipe_avcdec->input_flow = flow_def;
    upipe_avcdec->output_flow = NULL;
    upipe_avcdec->pixfmt = NULL;
    upipe_avcdec->counter = 0;
    upipe_avcdec->close = false;

    upipe_avcdec->index_rap = 0;
    upipe_avcdec->prev_rap = 0;
    upipe_avcdec->next_pts = UINT64_MAX;
    upipe_avcdec->next_pts_sys = UINT64_MAX;

    /* Increment our refcount so the context will have time to be closed
     * (decremented in @ref upipe_avcdec_do_av_deal) */
    upipe_use(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avcdec_mgr = {
    .signature = UPIPE_AVCDEC_SIGNATURE,

    .upipe_alloc = upipe_avcdec_alloc,
    .upipe_input = upipe_avcdec_input,
    .upipe_control = upipe_avcdec_control,
    .upipe_free = upipe_avcdec_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for avcdec pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcdec_mgr_alloc(void)
{
    /* We close the context even if it was not opened because it supposedly
     * "frees allocated structures" */
    return upipe_proxy_mgr_alloc(&upipe_avcdec_mgr, upipe_avcdec_close);
}
