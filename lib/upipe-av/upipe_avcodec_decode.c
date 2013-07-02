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
 * @short Upipe avcodec decode module
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
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
#include <upipe-av/upipe_av_pixfmt.h>
#include "upipe_av_internal.h"

#define EXPECTED_FLOW "block."

/** @hidden */
static void upipe_avcdec_reset_upump_mgr(struct upipe *upipe);
/** @hidden */
static bool upipe_avcdec_input_packet(struct upipe *upipe, struct uref *uref,
                                      struct upump *upump);

/** @internal @This are the parameters passed to avcodec_open2 by
 * upipe_avcodec_open_cb()
 */
struct upipe_avcodec_open_params {
    AVCodec *codec;
    uint8_t *extradata;
    int extradata_size;
};

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

    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** avcodec_open parameters */
    struct upipe_avcodec_open_params open_params;
    /** lowres param */
    int lowres;

    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_avcdec, upipe);
UPIPE_HELPER_FLOW(upipe_avcdec, EXPECTED_FLOW)
UPIPE_HELPER_OUTPUT(upipe_avcdec, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avcdec, ubuf_mgr);
UPIPE_HELPER_UPUMP_MGR(upipe_avcdec, upump_mgr, upipe_avcdec_reset_upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcdec, upump_av_deal, upump_mgr)
UPIPE_HELPER_SINK(upipe_avcdec, urefs, blockers, upipe_avcdec_input_packet)

/** @internal */
static bool upipe_avcdec_process_buf(struct upipe *upipe, uint8_t *buf,
                                    size_t size, struct upump *upump);

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

/** @internal @This is called by avcodec when allocating a new frame
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct ubuf *ubuf_pic;
    int width_aligned, height_aligned, i;
    const struct upipe_av_plane *planes = NULL;
    size_t stride = 0;

    frame->opaque = uref_dup(upipe_avcdec->uref);

    uint64_t framenum = 0;
    uref_pic_get_number(frame->opaque, &framenum);

    upipe_dbg_va(upipe, "Allocating frame for %u (%p) - %ux%u",
                 framenum, frame->opaque, frame->width, frame->height);

    if (unlikely(!upipe_avcdec->pixfmt)) {
        upipe_avcdec->pixfmt = upipe_av_pixfmt_from_ubuf_mgr(upipe_avcdec->ubuf_mgr);
        if (unlikely(!upipe_avcdec->pixfmt)) {
            upipe_err_va(upipe, "frame format of ubuf manager not recognized");
            return 0;
        }
    }
    if (context->pix_fmt != *upipe_avcdec->pixfmt->pixfmt) {
        upipe_err_va(upipe, "frame format not compatible (%s != %s",
                                       av_get_pix_fmt_name(context->pix_fmt),
                            av_get_pix_fmt_name(*upipe_avcdec->pixfmt->pixfmt));
        return 0;
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
            
            return 1; /* success */
        } else {
            upipe_dbg_va(upipe, "ubuf_pic_alloc(%d, %d) failed, fallback", width_aligned, height_aligned);
        }
    }

    /* default : DR failed or not available */
    return avcodec_default_get_buffer(context, frame);
}

/** @internal @This is called by avcodec when allocating a new audio buffer
 * Used with audio decoders.
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer_audio(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct ubuf *ubuf_samples;
    uint8_t *buf;
    int size;

    frame->opaque = uref_dup(upipe_avcdec->uref);

    /* direct rendering - allocate ubuf for audio */
    if (upipe_avcdec->context->codec->capabilities & CODEC_CAP_DR1) {
        
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
            
            return 1; /* success */
        } else {
            upipe_dbg_va(upipe, "ubuf allocation failed, fallback");
        }
    }

    /* default : DR failed or not available */
    return avcodec_default_get_buffer(context, frame);
}

/** @internal @This is called by avcodec when releasing a frame
 * @param context current avcodec context
 * @param frame avframe handler released by avcodec black magic box
 */
static void upipe_avcdec_release_buffer(struct AVCodecContext *context, AVFrame *frame)
{
    struct upipe *upipe = context->opaque;
    struct uref *uref = frame->opaque;
    const struct upipe_av_plane *planes = NULL;
    int i;

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_dbg_va(upipe, "Releasing frame %u (%p)", (uint64_t) framenum, uref);

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
        if (upipe_avcdec->open_params.extradata) {
            free(upipe_avcdec->open_params.extradata);
        }
        memset(&upipe_avcdec->open_params, 0, sizeof(struct upipe_avcodec_open_params));
    }
}

/** @internal @This configures a new codec context
 *
 * @param upipe description structure of the pipe
 * @param codec avcodec description structure
 * @param extradata pointer to extradata buffer
 * @param extradata_size extradata size
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_avcdec_open_codec(struct upipe *upipe, AVCodec *codec,
                                    uint8_t *extradata, int extradata_size)
{
    AVCodecContext *context = NULL;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    assert(upipe);

    /* close previously opened context */
    if (unlikely(upipe_avcdec->context)) {
        /* first send empty packet to flush retained frames */
        upipe_dbg(upipe, "flushing frames in decoder");
        while (upipe_avcdec_process_buf(upipe, NULL, 0, NULL));

        /* now close codec and free extradata if any */
        upipe_notice_va(upipe, "avcodec context (%s) closed (%d)",
                    upipe_avcdec->context->codec->name, upipe_avcdec->counter);
        avcodec_close(upipe_avcdec->context);
        if (upipe_avcdec->context->extradata_size > 0) {
            free(upipe_avcdec->context->extradata);
        }
        av_free(upipe_avcdec->context);
        upipe_avcdec->context = NULL;
        upipe_avcdec_store_flow_def(upipe, NULL);
    }

    /* just closing, that's all */
    if (!codec) {
        upipe_release(upipe);
        return false;
    }

    /* allocate and configure codec context */
    context = avcodec_alloc_context3(codec);
    if (unlikely(!context)) {
        upipe_throw_aerror(upipe);
        upipe_release(upipe);
        return false;
    }
    context->opaque = upipe;
    context->extradata = extradata;
    context->extradata_size = extradata_size;

    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            if (upipe_avcdec->lowres > codec->max_lowres) {
                upipe_warn_va(upipe, "Unsupported lowres (%d > %hhu), setting to %hhu",
                    upipe_avcdec->lowres, codec->max_lowres, codec->max_lowres);
                upipe_avcdec->lowres = codec->max_lowres;
            }

            context->get_buffer = upipe_avcdec_get_buffer;
            context->release_buffer = upipe_avcdec_release_buffer;
            context->flags |= CODEC_FLAG_EMU_EDGE;
            context->lowres = upipe_avcdec->lowres;
            //context->skip_loop_filter = AVDISCARD_ALL;

            if (!upipe_avcdec->output_flow) {
                struct uref *outflow = uref_dup(upipe_avcdec->input_flow);
                uref_flow_set_def(outflow, "pic.");
                uref_pic_flow_set_macropixel(outflow, 1);
                uref_pic_flow_set_planes(outflow, 0);
                upipe_avcdec_store_flow_def(upipe, outflow);
            }

            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            context->get_buffer = upipe_avcdec_get_buffer_audio;
            /* TODO: set attributes/need a real ubuf_audio structure (?) */
            if (!upipe_avcdec->output_flow) {
                #if 0
                struct uref *outflow = uref_sound_flow_alloc_def(upipe_avcdec->uref_mgr,
                            context->channels,
                            av_get_bytes_per_sample(context->sample_fmt));
                #else

                struct uref *outflow = uref_dup(upipe_avcdec->input_flow);
                uref_flow_set_def(outflow, "sound.");

                #endif
#if 0
                uref_sound_flow_set_channels(outflow, context->channels);
                uref_sound_flow_set_sample_size(outflow,
                                         av_get_bytes_per_sample(context->sample_fmt));
                uref_sound_flow_set_rate(outflow, context->sample_rate);
#endif

                upipe_avcdec_store_flow_def(upipe, outflow);
            }

            break;
        }
        default: {
            av_free(context);
            upipe_err_va(upipe, "Unsupported media type (%d)", codec->type);
            upipe_release(upipe);
            return false;
            break;
        }
    }

    /* open new context */
    if (unlikely(avcodec_open2(context, codec, NULL) < 0)) {
        upipe_warn(upipe, "could not open codec");
        av_free(context);
        upipe_release(upipe);
        return false;
    }

    upipe_avcdec->context = context;
    upipe_avcdec->counter = 0;
    upipe_notice_va(upipe, "codec %s (%s) %d opened", codec->name, 
            codec->long_name, codec->id);

    upipe_release(upipe);
    return true;
}

/** @internal @This is the open_codec upump callback
 * It calls _open_codec_cb.
 *
 * @param upump description structure of the pump
 */
static void upipe_avcdec_open_codec_cb(struct upump *upump)
{
    assert(upump);
    struct upipe *upipe = upump_get_opaque(upump, struct upipe*);
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcdec->open_params;
    struct upump *upump_av_deal = upipe_avcdec->upump_av_deal;

    /* check udeal */
    if (upump_av_deal) {
        if (unlikely(!upipe_av_deal_grab())) {
            upipe_dbg(upipe, "could not grab resource, return");
            return;
        }
        upipe_avcdec->upump_av_deal = NULL;
    }

    /* real open_codec function */
    bool ret = upipe_avcdec_open_codec(upipe, params->codec,
                  params->extradata, params->extradata_size);

    /* clean dealer */
    upipe_av_deal_yield(upump_av_deal);
    upump_free(upump_av_deal);
    upump_av_deal = NULL;

    if (!ret) {
        return;
    }

    upipe_avcdec_unblock_sink(upipe);
    upipe_avcdec_output_sink(upipe);
}

/** @internal @This copies extradata
 *
 * @param upipe description structure of the pipe
 * @param extradata pointer to extradata buffer
 * @param size extradata size
 * @return false if the buffer couldn't be accepted
 */
static uint8_t *upipe_avcdec_copy_extradata(struct upipe *upipe,
                                            const uint8_t *extradata, int size)
{
    uint8_t *buf;
    if (!extradata || size <= 0) {
        return NULL;
    }

    buf = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) {
        upipe_throw_aerror(upipe);
        return NULL;
    }

    memset(buf+size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(buf, extradata, size);

    upipe_dbg_va(upipe, "Received extradata (%d bytes)", size);
    return buf;
}

/** @internal @This configures a new codec context
 *
 * @param upipe description structure of the pipe
 * @param codec_def codec defintion string
 * @param extradata pointer to extradata buffer
 * @param extradata_size extradata size
 * @return false if the buffer couldn't be accepted
 */
static bool _upipe_avcdec_set_codec(struct upipe *upipe, const char *codec_def,
                                     uint8_t *extradata, int extradata_size)
{
    AVCodec *codec = NULL;
    int codec_id = 0;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcdec->open_params;
    uint8_t *extradata_padded = NULL;
 
    /* find codec */
    if (codec_def) {
        codec_id = upipe_av_from_flow_def(codec_def);
        if (unlikely(!codec_id)) {
            upipe_warn_va(upipe, "codec %s not found", codec_def);
        }
        codec = avcodec_find_decoder(codec_id);
        if (unlikely(!codec)) {
            upipe_warn_va(upipe, "codec %s (%d) not found", codec_def, codec_id);
        }
    }

    /* copy extradata if present */
    if (extradata && extradata_size > 0) {
        extradata_padded = upipe_avcdec_copy_extradata(upipe,
                                    extradata, extradata_size);
        if (unlikely(!extradata_padded)) {
            extradata_size = 0;
        }
    }

    /* use udeal/upump callback if available */
    if (upipe_avcdec->upump_mgr) {
        upipe_dbg(upipe, "upump_mgr present, using udeal");

        if (unlikely(upipe_avcdec->upump_av_deal)) {
            upipe_dbg(upipe, "previous upump_av_deal still running, cleaning first");
            upipe_avcdec_abort_av_deal(upipe);
        } else {
            upipe_use(upipe);
        }

        struct upump *upump_av_deal = upipe_av_deal_upump_alloc(upipe_avcdec->upump_mgr,
                                                     upipe_avcdec_open_codec_cb, upipe);
        if (unlikely(!upump_av_deal)) {
            upipe_err(upipe, "can't create dealer");
            upipe_throw_upump_error(upipe);
            return false;
        }
        upipe_avcdec->upump_av_deal = upump_av_deal;

        memset(params, 0, sizeof(struct upipe_avcodec_open_params));
        params->codec = codec;
        params->extradata = extradata_padded;
        params->extradata_size = extradata_size;

        /* fire */
        upipe_av_deal_start(upump_av_deal);
        return true;

    } else {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcdec_open");
        upipe_use(upipe);
        return upipe_avcdec_open_codec(upipe, codec, extradata_padded, extradata_size);
    }
}

/** @internal @This sets the index_rap attribute.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_avcdec_set_index_rap(struct upipe *upipe, struct uref *uref)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    uint64_t rap = 0;

    uref_clock_get_systime_rap(uref, &rap);
    if (unlikely(rap != upipe_avcdec->prev_rap)) {
        upipe_avcdec->prev_rap = rap;
        upipe_avcdec->index_rap = 0;
    }
    uref_clock_set_index_rap(uref, upipe_avcdec->index_rap);
    upipe_avcdec->index_rap++;

}

/** @internal @This outputs video frames
 *
 * @param upipe description structure of the pipe
 * @param frame AVFrame structure
 * @param upump upump structure
 */
static void upipe_avcdec_output_frame(struct upipe *upipe, AVFrame *frame,
                                      struct upump *upump)
{
    struct ubuf *ubuf;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    const struct upipe_av_plane *planes = upipe_avcdec->pixfmt->planes;
    struct uref *uref = uref_dup(frame->opaque);

    uint8_t *data, *src, hsub, vsub;
    const char *chroma = NULL; 
    size_t sstride, dstride;
    int i, j;
    struct urational aspect;

    /* if uref has no attached ubuf (ie DR not supported) */
    if (unlikely(!uref->ubuf)) {
        ubuf = ubuf_pic_alloc(upipe_avcdec->ubuf_mgr, frame->width, frame->height);
        if (!ubuf) {
            upipe_throw_aerror(upipe);
            return;
        }

        /* iterate through planes and copy data */
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

    /* set aspect-ratio */
    aspect.den = 0; /* null denom is invalid */
    if (upipe_avcdec->context->sample_aspect_ratio.den) {
        aspect.num = upipe_avcdec->context->sample_aspect_ratio.num;
        aspect.den = upipe_avcdec->context->sample_aspect_ratio.den;
    } else if (frame->sample_aspect_ratio.den) {
        aspect.num = frame->sample_aspect_ratio.num;
        aspect.den = frame->sample_aspect_ratio.den;
    }
    if (aspect.den) {
        urational_simplify(&aspect);
        uref_pic_set_aspect(uref, aspect);
    }

    /* index rap attribute */
    upipe_avcdec_set_index_rap(upipe, uref);

    /* DTS has no meaning from now on */
    uref_clock_delete_dts(uref);

    uint64_t pts;
    if (!uref_clock_get_pts(uref, &pts)) {
        pts = upipe_avcdec->next_pts;
        if (pts != UINT64_MAX)
            uref_clock_set_pts(uref, pts);
    }

    uint64_t duration;
    if (pts != UINT64_MAX && uref_clock_get_duration(uref, &duration))
        upipe_avcdec->next_pts = pts + duration;
    else
        upipe_warn(upipe, "couldn't determine next_pts");

    upipe_avcdec_output(upipe, uref, upump);
}

/** @internal @This outputs audio buffers
 *
 * @param upipe description structure of the pipe
 * @param frame AVFrame structure
 * @param upump upump structure
 */
static void upipe_avcdec_output_audio(struct upipe *upipe, AVFrame *frame,
                                     struct upump *upump)
{
    struct ubuf *ubuf;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct uref *uref = uref_dup(frame->opaque);
    int bufsize = -1, avbufsize;
    size_t size = 0;
    uint8_t *buf;
    AVCodecContext *context = upipe_avcdec->context;

    /* fetch audio sample size (in case it has been reduced) */
    avbufsize = av_samples_get_buffer_size(NULL, context->channels,
                       frame->nb_samples, context->sample_fmt, 1);

    /* if uref has no attached ubuf (ie DR not supported) */
    if (unlikely(!uref->ubuf)) {
        ubuf = ubuf_block_alloc(upipe_avcdec->ubuf_mgr, avbufsize);
        if (unlikely(!ubuf)) {
            upipe_throw_aerror(upipe);
            return;
        }

        ubuf_block_write(ubuf, 0, &bufsize, &buf);
        memcpy(buf, frame->data[0], bufsize);

        uref_attach_ubuf(uref, ubuf);
    }

    /* unmap, reduce block if needed */
    uref_block_unmap(uref, 0);
    uref_block_size(uref, &size);
    if (unlikely(size != avbufsize)) {
        uref_block_resize(uref, 0, avbufsize);
    }

    /* samples in uref */
    uref_sound_flow_set_samples(uref, frame->nb_samples);

    /* index rap attribute */
    upipe_avcdec_set_index_rap(upipe, uref);

    upipe_avcdec_output(upipe, uref, upump);
}

/** @internal @This handles buffers once stripped from uref.
 *
 * @param upipe description structure of the pipe
 * @param buf buffer containing packet
 * @param size buffer size before padding
 * @param upump upump structure
 */
static bool upipe_avcdec_process_buf(struct upipe *upipe, uint8_t *buf,
                                     size_t size, struct upump *upump)
{
    int gotframe = 0, len;
    AVPacket avpkt;
    AVFrame *frame; 
    uint64_t framenum = 0;

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    assert(upipe);

    /* init avcodec packed and attach input buffer */
    av_init_packet(&avpkt);
    avpkt.size = size;
    avpkt.data = buf;

    frame = upipe_avcdec->frame;

    switch (upipe_avcdec->context->codec->type) {
        case AVMEDIA_TYPE_VIDEO: {

            len = avcodec_decode_video2(upipe_avcdec->context, frame, &gotframe, &avpkt);
            if (len < 0) {
                upipe_warn(upipe, "Error while decoding frame");
            }

            /* output frame if any has been decoded */
            if (gotframe) {
                uref_pic_get_number(frame->opaque, &framenum);

                upipe_dbg_va(upipe, "%u\t - Picture decoded ! %dx%d - %u",
                        upipe_avcdec->counter, frame->width, frame->height, (uint64_t) framenum);

                upipe_avcdec_output_frame(upipe, frame, upump);
                return true;
            } else {
                return false;
            }
        }

        case AVMEDIA_TYPE_AUDIO: {
            len = avcodec_decode_audio4(upipe_avcdec->context, frame, &gotframe, &avpkt);
            if (len < 0) {
                upipe_warn(upipe, "Error while decoding frame");
            }

            /* output samples if any has been decoded */
            if (gotframe) {
                upipe_avcdec_output_audio(upipe, frame, upump);
                return true;
            } else {
                return false;
            }
        }

        default: {
            /* should never be here */
            upipe_err_va(upipe, "Unsupported media type (%d)",
                                    upipe_avcdec->context->codec->type);
            return false;
        }
    }
}

/** @internal @This handles packets.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 * @return true if the output could be written
 */
static bool upipe_avcdec_input_packet(struct upipe *upipe, struct uref *uref,
                                      struct upump *upump)
{
    uint8_t *inbuf;
    size_t insize = 0;

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    assert(upipe);
    assert(uref);

    if (unlikely(upipe_avcdec->upump_av_deal)) {
        /* pending open_codec callback */
        upipe_dbg(upipe, "Received packet while open_codec pending");
        return false;
    }

    if (!upipe_avcdec->context) {
        uref_free(uref);
        upipe_warn(upipe, "Received packet but decoder is not initialized");
        return true;
    }

    /* avcodec input buffer needs to be at least 4-byte aligned and
       FF_INPUT_BUFFER_PADDING_SIZE larger than actual input size.
       Thus, extract ubuf content in a properly allocated buffer.
       Padding must be zeroed. */
    uref_block_size(uref, &insize);
    if (unlikely(!insize)) {
        upipe_warn(upipe, "Received packet with size 0, dropping");
        uref_free(uref);
        return true;
    }

    upipe_dbg_va(upipe, "Received packet %u - size : %u", upipe_avcdec->counter, insize);
    inbuf = malloc(insize + FF_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(!inbuf)) {
        upipe_throw_aerror(upipe);
        return true;
    }
    memset(inbuf, 0, insize + FF_INPUT_BUFFER_PADDING_SIZE);
    uref_block_extract(uref, 0, insize, inbuf); 
    ubuf_free(uref_detach_ubuf(uref));

    uref_pic_set_number(uref, upipe_avcdec->counter);

    /* Track current uref in pipe structure - required for buffer allocation
     * in upipe_avcdec_get_buffer */
    upipe_avcdec->uref = uref;

    upipe_avcdec_process_buf(upipe, inbuf, insize, upump);

    free(inbuf);
    uref_free(uref);
    upipe_avcdec->counter++;
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
    if (unlikely(!uref->ubuf)) {
        upipe_avcdec_output(upipe, uref, upump);
        return;
    }

    if (unlikely(!upipe_avcdec_check_sink(upipe) ||
                 !upipe_avcdec_input_packet(upipe, uref, upump))) {
        upipe_avcdec_block_sink(upipe, upump);
        upipe_avcdec_hold_sink(upipe, uref);
    }
}

/** @internal @This resets upump_mgr-related fields.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_reset_upump_mgr(struct upipe *upipe)
{
    upipe_avcdec_set_upump_av_deal(upipe, NULL);
    upipe_avcdec_abort_av_deal(upipe);
}

/** @internal @This returns the current codec definition string
 */
static bool _upipe_avcdec_get_codec(struct upipe *upipe, const char **codec_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    assert(codec_p);
    if (upipe_avcdec->context && upipe_avcdec->context->codec) {
        *codec_p = upipe_av_to_flow_def(upipe_avcdec->context->codec->id);
    } else {
        *codec_p = NULL;
        return false;
    }
    return true;
}

/** @This sets the low resolution parameter, if supported by codec.
 * If some codec is already used, it is re-opened.
 *
 * @param upipe description structure of the pipe
 * @param lowres lowres parameter (0=disabled)
 * @return false in case of error
 */
static bool _upipe_avcdec_set_lowres(struct upipe *upipe, int lowres)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    const char *codec_def;
    bool ret = true;

    if (lowres < 0) {
        upipe_warn_va(upipe, "Invalid lowres parameter (%d)", lowres);
        return false;
    }
    upipe_avcdec->lowres = lowres;
    upipe_dbg_va(upipe, "Requesting lowres %d", lowres);

    if (upipe_avcdec->context && upipe_avcdec->context->codec) {
        codec_def = upipe_av_to_flow_def(upipe_avcdec->context->codec->id);
        ret = _upipe_avcdec_set_codec(upipe, codec_def,
                                      upipe_avcdec->context->extradata,
                                      upipe_avcdec->context->extradata_size);
    }
    return ret;
}

/** @internal @This returns the lowres parameter
 */
static bool _upipe_avcdec_get_lowres(struct upipe *upipe, int *lowres_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    assert(lowres_p);
    *lowres_p = upipe_avcdec->lowres;
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
            return upipe_avcdec_set_upump_mgr(upipe, upump_mgr);
        }


        case UPIPE_AVCDEC_GET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDEC_SIGNATURE);
            const char **url_p = va_arg(args, const char **);
            return _upipe_avcdec_get_codec(upipe, url_p);
        }
        case UPIPE_AVCDEC_SET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDEC_SIGNATURE);
            const char *codec = va_arg(args, const char *);
            uint8_t *extradata = va_arg(args, uint8_t *);
            int size = va_arg(args, int);
            return _upipe_avcdec_set_codec(upipe, codec, extradata, size);
        }
        case UPIPE_AVCDEC_GET_LOWRES: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDEC_SIGNATURE);
            int *lowres_p = va_arg(args, int *);
            return _upipe_avcdec_get_lowres(upipe, lowres_p);
        }
        case UPIPE_AVCDEC_SET_LOWRES: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCDEC_SIGNATURE);
            unsigned int lowres = va_arg(args, int);
            return _upipe_avcdec_set_lowres(upipe, lowres);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_free(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (upipe_avcdec->context) {
        _upipe_avcdec_set_codec(upipe, NULL, NULL, 0);
        return; /* _set_codec() calls _use()/_release() */
    }

    upipe_throw_dead(upipe);

    uref_free(upipe_avcdec->input_flow);
    if (upipe_avcdec->frame) {
        av_free(upipe_avcdec->frame);
    }

    upipe_avcdec_abort_av_deal(upipe);
    upipe_avcdec_clean_sink(upipe);
    upipe_avcdec_clean_output(upipe);
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

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    upipe_avcdec_init_ubuf_mgr(upipe);
    upipe_avcdec_init_upump_mgr(upipe);
    upipe_avcdec_init_upump_av_deal(upipe);
    upipe_avcdec_init_output(upipe);
    upipe_avcdec_init_sink(upipe);
    upipe_avcdec->input_flow = flow_def;
    upipe_avcdec->context = NULL;
    upipe_avcdec->pixfmt = NULL;
    upipe_avcdec->frame = avcodec_alloc_frame();
    upipe_avcdec->lowres = 0;

    upipe_avcdec->index_rap = 0;
    upipe_avcdec->prev_rap = 0;
    upipe_avcdec->next_pts = UINT64_MAX;

    upipe_throw_ready(upipe);

    const char *def = NULL;
    if (likely(uref_flow_get_def(flow_def, &def)))
        def += strlen(EXPECTED_FLOW);
    _upipe_avcdec_set_codec(upipe, def, NULL, 0);

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
    return &upipe_avcdec_mgr;
}
