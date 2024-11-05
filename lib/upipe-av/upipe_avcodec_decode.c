/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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

#include "upipe/uclock.h"
#include "upipe/ubuf.h"
#include "upipe/uref.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_pic_flow_formats.h"
#include "upipe/uref_sound.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_block.h"
#include "upipe/uref_clock.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_flow_def_check.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-av/upipe_avcodec_decode.h"
#include "upipe-av/ubuf_av.h"
#include "upipe-framers/uref_h26x.h"
#include "upipe-ts/uref_ts_flow.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mastering_display_metadata.h>
#include "upipe-av/upipe_av_pixfmt.h"
#include "upipe-av/upipe_av_samplefmt.h"
#include "upipe_av_internal.h"

#include <bitstream/dvb/sub.h>

#define EXPECTED_FLOW_DEF "block."

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 63, 100)
#define USE_COPY_OPAQUE
#endif

/** @hidden */
static int upipe_avcdec_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static bool upipe_avcdec_decode_avpkt(struct upipe *upipe, AVPacket *avpkt,
                                      struct upump **upump_p);
/** @hidden */
static bool upipe_avcdec_decode(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);

/** upipe_avcdec structure with avcdec parameters */
struct upipe_avcdec {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
    /** attributes describing output format */
    struct uref *flow_def_format;
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** structure to check input flow def */
    struct uref *flow_def_check;
    /** structure provided by the ubuf_mgr request */
    struct uref *flow_def_provided;
    /** output flow */
    struct uref *flow_def;
    /** output pipe */
    struct upipe *output;
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

    /** upump mgr */
    struct upump_mgr *upump_mgr;
    /** pixel format used for the ubuf manager */
    enum AVPixelFormat pix_fmt;
    /** sample format used for the ubuf manager */
    enum AVSampleFormat sample_fmt;
    /** number of channels used for the ubuf manager */
    unsigned int channels;

    /** avcodec_open watcher */
    struct upump *upump_av_deal;
    /** temporary uref storage (used during udeal) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** frame counter */
    uint64_t counter;
    /** rap offset */
    uint8_t index_rap;
    /** I frame rap */
    uint64_t iframe_rap;
    /** latest incoming uref */
    struct uref *uref;
    /** last PTS */
    uint64_t last_pts;
    /** last PTS (systime time) */
    uint64_t last_pts_sys;
    /** next PTS */
    uint64_t next_pts;
    /** next PTS (systime time) */
    uint64_t next_pts_sys;
    /** latency in the input flow */
    uint64_t input_latency;
    /** drift rate */
    struct urational drift_rate;
    /** last input DTS */
    uint64_t input_dts;
    /** last input DTS (system time) */
    uint64_t input_dts_sys;

    /** configured hardware device type */
    enum AVHWDeviceType hw_device_type;
    /** hardware device, or NULL for default device */
    char *hw_device;
    /** hw pixel format */
    enum AVPixelFormat hw_pix_fmt;
    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** avcodec packet */
    AVPacket *avpkt;
    /** true if the context will be closed */
    bool close;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avcdec, upipe, UPIPE_AVCDEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_avcdec, urefcount, upipe_avcdec_close)
UPIPE_HELPER_VOID(upipe_avcdec)
UPIPE_HELPER_OUTPUT(upipe_avcdec, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_avcdec, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_avcdec, flow_def_check)

UPIPE_HELPER_UBUF_MGR(upipe_avcdec, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_avcdec_check,
                      upipe_avcdec_register_output_request,
                      upipe_avcdec_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_avcdec, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcdec, upump_av_deal, upump_mgr)
UPIPE_HELPER_INPUT(upipe_avcdec, urefs, nb_urefs, max_urefs, blockers, upipe_avcdec_decode)

/** @hidden */
static void upipe_avcdec_free(struct upipe *upipe);

/** @internal @This provides a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_avcdec_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (flow_format != NULL) {
        uref_free(upipe_avcdec->flow_def_provided);
        upipe_avcdec->flow_def_provided = flow_format;
    }
    return UBASE_ERR_NONE;
}

static void upipe_av_uref_pic_free(void *opaque, uint8_t *data);

/* Documentation from libavcodec.h (get_buffer) :
 * The function will set AVFrame.data[], AVFrame.linesize[].
 * AVFrame.extended_data[] must also be set, but it should be the same as
 * AVFrame.data[] except for planar audio with more channels than can fit
 * in AVFrame.data[].  In that case, AVFrame.data[] shall still contain as
 * many data pointers as it can hold.  if AV_CODEC_CAP_DR1 is not set then
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

static void buffer_uref_free(void *opaque, uint8_t *data)
{
    struct uref *uref = opaque;
    struct uref *flow_def_attr = uref_from_uchain(uref->uchain.next);
    uref_free(flow_def_attr);
    uref_free(uref);
}

/** @internal @This is called by avcodec when allocating a new picture.
 *
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer_pic(struct AVCodecContext *context,
                                       AVFrame *frame,
                                       int flags)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct uref *uref;
    bool internal_frame = false;

    if (frame->opaque_ref == NULL) {
        if (unlikely(upipe_avcdec->uref == NULL)) {
            upipe_warn(upipe, "get_buffer called without uref");
            return -1;
        }

        uref = uref_dup(upipe_avcdec->uref);
        if (uref == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return -1;
        }
        frame->opaque_ref = av_buffer_create(NULL, 0, buffer_uref_free,
                                             uref, 0);
        if (frame->opaque_ref == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return -1;
        }
        internal_frame = true;
    }

    uref = av_buffer_get_opaque(frame->opaque_ref);

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_verbose_va(upipe, "Allocating %s frame for %"PRIu64" (%p) - %dx%d",
                     av_get_pix_fmt_name(frame->format), framenum,
                     uref, frame->width, frame->height);

    if (internal_frame &&
        (frame->format == upipe_avcdec->hw_pix_fmt ||
         !(context->codec->capabilities & AV_CODEC_CAP_DR1))) {
        int ret = avcodec_default_get_buffer2(context, frame, flags);
        if (ret < 0) {
            upipe_err_va(upipe, "avcodec_default_get_buffer2 failed: %s",
                         av_err2str(ret));
            upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
        }
        return ret;
    }

    /* Check if we have a new pixel format. */
    if (unlikely(frame->format != upipe_avcdec->pix_fmt)) {
        ubuf_mgr_release(upipe_avcdec->ubuf_mgr);
        upipe_avcdec->ubuf_mgr = NULL;
        upipe_avcdec->pix_fmt = frame->format;
    }

    /* Use avcodec width/height alignment, then resize pic. */
    int width_aligned = frame->width, height_aligned = frame->height;
    int linesize_align[AV_NUM_DATA_POINTERS];
    memset(linesize_align, 0, sizeof(linesize_align));
    avcodec_align_dimensions2(context, &width_aligned, &height_aligned,
                              linesize_align);
    int align = linesize_align[0];
    for (int i = 1; i < AV_NUM_DATA_POINTERS; i++)
        if (linesize_align[i] > 0)
            align = align * linesize_align[i] /
                ubase_gcd(align, linesize_align[i]);

    /* Prepare flow definition attributes. */
    struct uref *flow_def_attr = upipe_avcdec_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def_attr == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return -1;
    }
    enum AVPixelFormat pix_fmt = upipe_avcdec->pix_fmt;
    if (context->hw_frames_ctx) {
        AVHWFramesContext *hw_frames_ctx =
            (AVHWFramesContext *) context->hw_frames_ctx->data;
        pix_fmt = hw_frames_ctx->sw_format;
    }
    if (unlikely(!ubase_check(upipe_av_pixfmt_to_flow_def(pix_fmt,
                                                          flow_def_attr)))) {
        uref_free(uref);
        uref_free(flow_def_attr);
        upipe_err_va(upipe, "unhandled pixel format '%s' (%d)",
                     av_get_pix_fmt_name(pix_fmt) ?: "unknown", pix_fmt);
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return -1;
    }

    if (frame->format == upipe_avcdec->hw_pix_fmt)
        uref_pic_flow_set_surface_type_va(flow_def_attr, "av.%s",
                                          av_get_pix_fmt_name(frame->format));

    UBASE_FATAL(upipe, uref_pic_flow_set_align(flow_def_attr, align))
    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def_attr, context->width))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def_attr, context->height))
    UBASE_FATAL(upipe, uref_pic_flow_set_hsize_visible(flow_def_attr, context->width))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize_visible(flow_def_attr, context->height))
    struct urational fps;
    if (!ubase_check(uref_pic_flow_get_fps(upipe_avcdec->flow_def_input, &fps))) {
        fps.num = context->framerate.num;
        fps.den = context->framerate.den;
    }
    if (fps.num && fps.den) {
        urational_simplify(&fps);
        UBASE_FATAL(upipe, uref_pic_flow_set_fps(flow_def_attr, fps))

        uint64_t latency = upipe_avcdec->input_latency +
                           context->delay * UCLOCK_FREQ * fps.den / fps.num;
        if (context->active_thread_type == FF_THREAD_FRAME &&
            context->thread_count != -1)
            latency += context->thread_count * UCLOCK_FREQ * fps.den / fps.num;
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def_attr, latency))
    }
    /* set aspect-ratio */
    if (frame->sample_aspect_ratio.num) {
        struct urational sar;
        sar.num = frame->sample_aspect_ratio.num;
        sar.den = frame->sample_aspect_ratio.den;
        urational_simplify(&sar);
        UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def_attr, sar))
    } else if (context->sample_aspect_ratio.num) {
        struct urational sar = {
            .num = context->sample_aspect_ratio.num,
            .den = context->sample_aspect_ratio.den
        };
        urational_simplify(&sar);
        UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def_attr, sar))
    }

    if (context->color_range == AVCOL_RANGE_JPEG)
        uref_pic_flow_set_full_range(flow_def_attr);

    AVFrameSideData *sd = av_frame_get_side_data(
        frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (sd) {
        AVContentLightMetadata *clm = (AVContentLightMetadata *)sd->data;
        UBASE_FATAL(upipe, uref_pic_flow_set_max_cll(
                flow_def_attr, clm->MaxCLL))
        UBASE_FATAL(upipe, uref_pic_flow_set_max_fall(
                flow_def_attr, clm->MaxFALL))
    } else if (!frame->key_frame) {
        uint64_t max_cll;
        if (ubase_check(uref_pic_flow_get_max_cll(
                    upipe_avcdec->flow_def_format, &max_cll))) {
            UBASE_FATAL(upipe, uref_pic_flow_set_max_cll(
                    flow_def_attr, max_cll))
        }
        uint64_t max_fall;
        if (ubase_check(uref_pic_flow_get_max_fall(
                    upipe_avcdec->flow_def_format, &max_fall))) {
            UBASE_FATAL(upipe, uref_pic_flow_set_max_fall(
                    flow_def_attr, max_fall))
        }
    }

    sd = av_frame_get_side_data(
        frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd) {
        AVMasteringDisplayMetadata *mdcv =
            (AVMasteringDisplayMetadata *)sd->data;
        AVRational chroma = { 1, 50000 };
        AVRational luma = { 1, 10000 };
        UBASE_FATAL(upipe, uref_pic_flow_set_mastering_display(flow_def_attr,
            &(struct uref_pic_mastering_display){
                .red_x = av_rescale_q(1, mdcv->display_primaries[0][0], chroma),
                .red_y = av_rescale_q(1, mdcv->display_primaries[0][1], chroma),
                .green_x = av_rescale_q(1, mdcv->display_primaries[1][0], chroma),
                .green_y = av_rescale_q(1, mdcv->display_primaries[1][1], chroma),
                .blue_x = av_rescale_q(1, mdcv->display_primaries[2][0], chroma),
                .blue_y = av_rescale_q(1, mdcv->display_primaries[2][1], chroma),
                .white_x = av_rescale_q(1, mdcv->white_point[0], chroma),
                .white_y = av_rescale_q(1, mdcv->white_point[1], chroma),
                .min_luminance = av_rescale_q(1, mdcv->min_luminance, luma),
                .max_luminance = av_rescale_q(1, mdcv->max_luminance, luma),
            }))
    } else if (!frame->key_frame) {
        const uint8_t *mdcv;
        size_t size;
        if (ubase_check(uref_pic_flow_get_mdcv(upipe_avcdec->flow_def_format,
                                               &mdcv, &size))) {
            UBASE_FATAL(upipe, uref_pic_flow_set_mdcv(
                    flow_def_attr, mdcv, size))
        }
    }

    if (unlikely(upipe_avcdec->ubuf_mgr != NULL &&
                 udict_cmp(upipe_avcdec->flow_def_format->udict,
                           flow_def_attr->udict))) {
        /* flow format changed */
        ubuf_mgr_release(upipe_avcdec->ubuf_mgr);
        upipe_avcdec->ubuf_mgr = NULL;
    }

    bool use_ubuf_av = upipe_avcdec->uref == NULL ||
        frame->format == upipe_avcdec->hw_pix_fmt ||
        !(context->codec->capabilities & AV_CODEC_CAP_DR1);

    if (unlikely(upipe_avcdec->ubuf_mgr == NULL)) {
        upipe_avcdec->flow_def_format = uref_dup(flow_def_attr);
        if (use_ubuf_av) {
            upipe_avcdec->ubuf_mgr = ubuf_av_mgr_alloc();
            upipe_avcdec->flow_def_provided = flow_def_attr;
            if (upipe_avcdec->ubuf_mgr == NULL) {
                uref_free(uref);
                return -1;
            }
        } else {
            if (unlikely(!upipe_avcdec_demand_ubuf_mgr(upipe, flow_def_attr))) {
                uref_free(uref);
                return -1;
            }
        }
    } else
        uref_free(flow_def_attr);

    flow_def_attr = uref_dup(upipe_avcdec->flow_def_provided);

    /* Allocate a ubuf */
    struct ubuf *ubuf;
    if (use_ubuf_av) {
        ubuf = ubuf_pic_av_alloc(upipe_avcdec->ubuf_mgr, frame);
        if (unlikely(ubuf == NULL)) {
            upipe_err_va(upipe, "cannot alloc ubuf for %s frame",
                         av_get_pix_fmt_name(pix_fmt));
            goto error;
        }
        av_buffer_unref(&frame->opaque_ref);
    } else {
        ubuf = ubuf_pic_alloc(upipe_avcdec->ubuf_mgr,
                              width_aligned, height_aligned);
        if (unlikely(ubuf == NULL)) {
            upipe_err_va(upipe, "cannot alloc ubuf");
            goto error;
        }

        ubuf_pic_clear(ubuf, 0, 0, -1, -1,
                       frame->color_range == AVCOL_RANGE_JPEG);
    }
    uref_attach_ubuf(uref, ubuf);

    /* Chain the new flow def attributes to the uref so we can apply them
     * later. */
    uref->uchain.next = uref_to_uchain(flow_def_attr);

    if (use_ubuf_av)
        return 0;

    /* Direct rendering */
    /* Iterate over the flow def attr because it's designed to be in the correct
     * chroma order, while the ubuf manager is not necessarily. */
    uint8_t planes;
    if (unlikely(!ubase_check(uref_pic_flow_get_planes(flow_def_attr, &planes))))
        goto error;

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *chroma;
        size_t stride = 0;
        uint8_t vsub = 1;
        if (unlikely(!ubase_check(uref_pic_flow_get_chroma(flow_def_attr, &chroma, plane)) ||
                     !ubase_check(ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1,
                                           &frame->data[plane])) ||
                     !ubase_check(ubuf_pic_plane_size(ubuf, chroma, &stride, NULL, &vsub,
                                          NULL)))) {
            // XXX: missing unmap and release av_buffer for previous planes
            goto error;
        }

        frame->linesize[plane] = stride;
        frame->buf[plane] = av_buffer_create((uint8_t*)chroma, 0,
                upipe_av_uref_pic_free, av_buffer_ref(frame->opaque_ref), 0);
    }

    frame->extended_data = frame->data;

    return 0; /* success */

error:
    uref_free(uref);
    uref_free(flow_def_attr);
    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    return -1;
}

static void upipe_av_uref_pic_free(void *opaque, uint8_t *data)
{
    AVBufferRef *opaque_ref = opaque;
    struct uref *uref = av_buffer_get_opaque(opaque_ref);
    uref_pic_plane_unmap(uref, (const char *)data, 0, 0, -1, -1);
    av_buffer_unref(&opaque_ref);
}

static void upipe_av_uref_sound_free(void *opaque, uint8_t *data)
{
    AVBufferRef *opaque_ref = opaque;
    struct uref *uref = av_buffer_get_opaque(opaque_ref);
    uref_sound_unmap(uref, 0, -1, AV_NUM_DATA_POINTERS);
    av_buffer_unref(&opaque_ref);
}

/** @internal @This is called by avcodec when allocating a new audio buffer.
 * Used with audio decoders.
 *
 * @param context current avcodec context
 * @param frame avframe handler entering avcodec black magic box
 */
static int upipe_avcdec_get_buffer_sound(struct AVCodecContext *context,
                                         AVFrame *frame, int flags)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct uref *uref;
    bool internal_frame = false;

    if (frame->opaque_ref == NULL) {
        if (unlikely(upipe_avcdec->uref == NULL)) {
            upipe_warn(upipe, "get_buffer called without uref");
            return -1;
        }

        uref = uref_dup(upipe_avcdec->uref);
        if (uref == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return -1;
        }
        frame->opaque_ref = av_buffer_create(NULL, 0, buffer_uref_free,
                                             uref, 0);
        if (frame->opaque_ref == NULL) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return -1;
        }
        internal_frame = true;
    }

    uref = av_buffer_get_opaque(frame->opaque_ref);

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_verbose_va(upipe, "Allocating frame for %"PRIu64" (%p)",
                     framenum, uref);

    if (internal_frame && !(context->codec->capabilities & AV_CODEC_CAP_DR1))
        return avcodec_default_get_buffer2(context, frame, flags);

    /* Check if we have a new sample format. */
    if (unlikely(context->sample_fmt != upipe_avcdec->sample_fmt ||
                 context->ch_layout.nb_channels != upipe_avcdec->channels)) {
        ubuf_mgr_release(upipe_avcdec->ubuf_mgr);
        upipe_avcdec->ubuf_mgr = NULL;
        upipe_avcdec->sample_fmt = context->sample_fmt;
        upipe_avcdec->channels = context->ch_layout.nb_channels;
    }

    /* Prepare flow definition attributes. */
    struct uref *flow_def_attr = upipe_avcdec_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def_attr == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return -1;
    }
    UBASE_FATAL(upipe, upipe_av_samplefmt_to_flow_def(flow_def_attr,
                                               upipe_avcdec->sample_fmt,
                                               context->ch_layout.nb_channels));
    /* at the moment sample_rate is not filled until the first output */
    if (context->sample_rate)
        UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def_attr,
                                              context->sample_rate))

    if (context->bits_per_raw_sample)
        UBASE_FATAL(upipe, uref_sound_flow_set_raw_sample_size(flow_def_attr,
                                              context->bits_per_raw_sample))

    if (context->frame_size)
        UBASE_FATAL(upipe, uref_sound_flow_set_samples(flow_def_attr,
                                                 context->frame_size))
    UBASE_FATAL(upipe, uref_sound_flow_set_align(flow_def_attr, 32))

    bool use_ubuf_av = upipe_avcdec->uref == NULL;

    if (unlikely(upipe_avcdec->ubuf_mgr == NULL)) {
        if (use_ubuf_av) {
            upipe_avcdec->ubuf_mgr = ubuf_av_mgr_alloc();
            upipe_avcdec->flow_def_provided = flow_def_attr;
            if (upipe_avcdec->ubuf_mgr == NULL) {
                uref_free(uref);
                return -1;
            }
        } else {
            if (unlikely(!upipe_avcdec_demand_ubuf_mgr(upipe, flow_def_attr))) {
                uref_free(uref);
                return -1;
            }
        }
    } else
        uref_free(flow_def_attr);

    flow_def_attr = uref_dup(upipe_avcdec->flow_def_provided);

    struct ubuf *ubuf;
    if (use_ubuf_av) {
        ubuf = ubuf_sound_av_alloc(upipe_avcdec->ubuf_mgr, frame);
        if (unlikely(ubuf == NULL)) {
            upipe_err(upipe, "cannot alloc sound ubuf");
            uref_free(uref);
            uref_free(flow_def_attr);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return -1;
        }
        av_buffer_unref(&frame->opaque_ref);
    } else {
        ubuf = ubuf_sound_alloc(upipe_avcdec->ubuf_mgr,
                                frame->nb_samples);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            uref_free(flow_def_attr);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return -1;
        }
    }
    uref_attach_ubuf(uref, ubuf);

    /* Chain the new flow def attributes to the uref so we can apply them
     * later. */
    uref->uchain.next = uref_to_uchain(flow_def_attr);

    if (use_ubuf_av)
        return 0;

    /* Direct rendering */
    if (unlikely(!ubase_check(ubuf_sound_write_uint8_t(ubuf, 0, -1, frame->data,
                                                 AV_NUM_DATA_POINTERS)))) {
        uref_free(uref);
        uref_free(flow_def_attr);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return -1;
    }

    frame->linesize[0] = av_get_bytes_per_sample(context->sample_fmt) *
                         frame->nb_samples;

    frame->buf[0] = av_buffer_create(frame->data[0],
            frame->linesize[0] * context->ch_layout.nb_channels,
            upipe_av_uref_sound_free, av_buffer_ref(frame->opaque_ref), 0);

    if (!av_sample_fmt_is_planar(context->sample_fmt))
        frame->linesize[0] *= context->ch_layout.nb_channels;

    frame->extended_data = frame->data;

    return 0; /* success */
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

static enum AVPixelFormat upipe_avcodec_get_format(AVCodecContext *context,
                                                   const enum AVPixelFormat *pix_fmts)
{
    struct upipe *upipe = context->opaque;
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    for (const enum AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
        if (*p == upipe_avcdec->hw_pix_fmt)
            return *p;

    upipe_warn_va(upipe, "failed to get hw surface format %s",
                  av_get_pix_fmt_name(upipe_avcdec->hw_pix_fmt));

    return AV_PIX_FMT_NONE;
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
        return false;
    }

    switch (context->codec->type) {
        case AVMEDIA_TYPE_SUBTITLE:
            context->get_buffer2 = NULL;
            break;
        case AVMEDIA_TYPE_VIDEO:
#ifndef USE_COPY_OPAQUE
            context->get_buffer2 = upipe_avcdec_get_buffer_pic;
#endif
            if (upipe_avcdec->hw_pix_fmt != AV_PIX_FMT_NONE)
                context->get_format = upipe_avcodec_get_format;
            break;
        case AVMEDIA_TYPE_AUDIO:
#ifndef USE_COPY_OPAQUE
            context->get_buffer2 = upipe_avcdec_get_buffer_sound;
#endif
            break;
        default:
            /* This should not happen */
            upipe_err_va(upipe, "Unsupported media type (%d)",
                         context->codec->type);
            return false;
    }

    /* open hardware decoder */
    int err;
    if (upipe_avcdec->hw_device_type != AV_HWDEVICE_TYPE_NONE) {
        if (unlikely((err = av_hwdevice_ctx_create(&context->hw_device_ctx,
                                                   upipe_avcdec->hw_device_type,
                                                   upipe_avcdec->hw_device,
                                                   NULL, 0)) < 0)) {
            upipe_warn_va(upipe, "could not create hw device context (%s)",
                          av_err2str(err));
            upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
            return false;
        }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 11, 100)
        context->extra_hw_frames = UPIPE_AV_EXTRA_HW_FRAMES;
#endif
        upipe_notice_va(upipe, "created %s hw device context (%s)",
                        av_hwdevice_get_type_name(upipe_avcdec->hw_device_type),
                        upipe_avcdec->hw_device ?: "default");
    }

#ifdef USE_COPY_OPAQUE
    context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
#endif

    /* open new context */
    if (unlikely((err = avcodec_open2(context, context->codec, NULL)) < 0)) {
        upipe_warn_va(upipe, "could not open codec (%s)", av_err2str(err));
        upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
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

    /* real open_codec function */
    bool ret = upipe_avcdec_do_av_deal(upipe);

    /* clean dealer */
    upipe_av_deal_yield(upump);
    upump_free(upipe_avcdec->upump_av_deal);
    upipe_avcdec->upump_av_deal = NULL;

    if (upipe_avcdec->close) {
        upipe_avcdec_free(upipe);
        return;
    }

    if (ret)
        upipe_avcdec_output_input(upipe);
    else
        upipe_avcdec_flush_input(upipe);
    upipe_avcdec_unblock_input(upipe);
    /* All packets have been output, release again the pipe that has been
     * used in @ref upipe_avcdec_start_av_deal. */
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
    upipe_avcdec_check_upump_mgr(upipe);
    if (upipe_avcdec->upump_mgr == NULL) {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcodec_open");
        upipe_avcdec_do_av_deal(upipe);
        if (upipe_avcdec->close)
            upipe_avcdec_free(upipe);
        return;
    }

    upipe_dbg(upipe, "upump_mgr present, using udeal");
    struct upump *upump_av_deal =
        upipe_av_deal_upump_alloc(upipe_avcdec->upump_mgr,
                upipe_avcdec_cb_av_deal, upipe, upipe->refcount);
    if (unlikely(!upump_av_deal)) {
        upipe_err(upipe, "can't create dealer");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_avcdec->upump_av_deal = upump_av_deal;
    /* Increment upipe refcount to avoid disappearing before all packets
     * have been sent. */
    upipe_use(upipe);
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
 * We close the context even if it was not opened because it supposedly
 * "frees allocated structures".
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_close(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (upipe_avcdec->context == NULL) {
        upipe_avcdec_free(upipe);
        return;
    }

    if (upipe_avcdec->context->codec->capabilities & AV_CODEC_CAP_DELAY) {
        /* Feed avcodec with NULL packet to output the remaining frames */
        upipe_avcdec_decode_avpkt(upipe, NULL, NULL);
    }
    upipe_avcdec->close = true;
    upipe_avcdec_start_av_deal(upipe);
}

/** @internal @This sets the various time attributes.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_avcdec_set_time_attributes(struct upipe *upipe,
                                             struct uref *uref)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    uint64_t duration, pts, pts_sys;

    if (ubase_check(uref_pic_get_key(uref))) {
        uref_clock_get_rap_sys(uref, &upipe_avcdec->iframe_rap);
        upipe_avcdec->index_rap = 0;
    } else if (upipe_avcdec->iframe_rap != UINT64_MAX)
        uref_clock_set_rap_sys(uref, upipe_avcdec->iframe_rap);
    UBASE_FATAL(upipe, uref_clock_set_index_rap(uref, upipe_avcdec->index_rap))
    upipe_avcdec->index_rap++;

    /* Rebase dates to PTS. */
    if (!ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        pts = upipe_avcdec->next_pts;
        if (pts != UINT64_MAX) {
            uref_clock_set_pts_prog(uref, pts);
        }
    } else if (upipe_avcdec->last_pts != UINT64_MAX &&
               pts < upipe_avcdec->last_pts) {
        upipe_warn_va(upipe, "PTS prog in the past, resetting (%"PRIu64" ms)",
                      (upipe_avcdec->last_pts - pts) * 1000 / UCLOCK_FREQ);
        pts = upipe_avcdec->last_pts + 1;
        uref_clock_set_pts_prog(uref, pts);
    } else
        uref_clock_rebase_pts_prog(uref);

    if (pts != UINT64_MAX &&
        upipe_avcdec->input_dts != UINT64_MAX &&
        upipe_avcdec->input_dts_sys != UINT64_MAX) {
        pts_sys = (int64_t)upipe_avcdec->input_dts_sys +
            ((int64_t)pts - (int64_t)upipe_avcdec->input_dts) *
            (int64_t)upipe_avcdec->drift_rate.num /
            (int64_t)upipe_avcdec->drift_rate.den;
        uref_clock_set_pts_sys(uref, pts_sys);
    } else if (!ubase_check(uref_clock_get_pts_sys(uref, &pts_sys))) {
        pts_sys = upipe_avcdec->next_pts_sys;
        if (pts_sys != UINT64_MAX) {
            uref_clock_set_pts_sys(uref, pts_sys);
        }
    } else if (upipe_avcdec->last_pts_sys != UINT64_MAX &&
               pts_sys < upipe_avcdec->last_pts_sys) {
        upipe_warn_va(upipe, "PTS sys in the past, resetting (%"PRIu64" ms)",
                      (upipe_avcdec->last_pts_sys - pts_sys) * 1000 /
                      UCLOCK_FREQ);
        pts_sys = upipe_avcdec->last_pts_sys + 1;
        uref_clock_set_pts_sys(uref, pts_sys);
    } else
        uref_clock_rebase_pts_sys(uref);

    uref_clock_rebase_pts_orig(uref);
    uref_clock_set_rate(uref, upipe_avcdec->drift_rate);

    /* compute next pts based on current frame duration */
    if (pts != UINT64_MAX && ubase_check(uref_clock_get_duration(uref, &duration))) {
        upipe_avcdec->last_pts = pts;
        upipe_avcdec->next_pts = pts + duration;
        if (pts_sys != UINT64_MAX) {
            upipe_avcdec->last_pts_sys = pts_sys;
            upipe_avcdec->next_pts_sys = pts_sys + duration;
        }
    } else {
        upipe_warn(upipe, "couldn't determine next_pts");
    }
}

/** @internal @This builds the flow definition packet for subtitles.
 *
 * @param upipe description structure of the pipe
 * @param flow_def_attr flow def attributes uref
 */
static void upipe_avcdec_build_flow_def_sub(struct upipe *upipe,
                                            struct uref *flow_def_attr)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct uref *flow_def =
        upipe_avcdec_store_flow_def_attr(upipe, flow_def_attr);
    if (flow_def == NULL)
        return;

    if (upipe_avcdec->context->codec_id == AV_CODEC_ID_DVB_TELETEXT) {
        uint8_t *txt_page;
        if (av_opt_get(upipe_avcdec->context, "txt_page",
                       AV_OPT_SEARCH_CHILDREN, &txt_page) >= 0 &&
            strlen((const char *) txt_page) == 3) {
            int d_magazine = txt_page[0] - '0';
            int d_page = strtoul((const char *) txt_page + 1, NULL, 16);
            if (d_magazine == 8)
                d_magazine = 0;
            av_free(txt_page);

            int d_type = 0;
            char d_lang[4];
            uint8_t languages = 0;
            uref_flow_get_languages(flow_def, &languages);
            for (uint8_t l = 0; l < languages; l++) {
                const char *lang;
                uint8_t type = 0;
                uint8_t magazine = 0;
                uint8_t page = 0;
                uref_flow_get_language(flow_def, &lang, l);
                uref_ts_flow_get_telx_type(flow_def, &type, l);
                uref_ts_flow_get_telx_magazine(flow_def, &magazine, l);
                uref_ts_flow_get_telx_page(flow_def, &page, l);
                if (magazine == d_magazine && page == d_page) {
                    strncpy(d_lang, lang, 3);
                    d_lang[3] = '\0';
                    d_type = type;
                }
                uref_flow_delete_language(flow_def, l);
                uref_ts_flow_delete_telx_type(flow_def, l);
                uref_ts_flow_delete_telx_magazine(flow_def, l);
                uref_ts_flow_delete_telx_page(flow_def, l);
            }

            if (d_type) {
                uref_flow_set_languages(flow_def, 1);
                uref_flow_set_language(flow_def, d_lang, 0);
                if (d_type == 5)
                    uref_flow_set_hearing_impaired(flow_def, 0);
                else
                    uref_flow_delete_hearing_impaired(flow_def, 0);
            }
        }
    }

    uref_block_flow_clear_format(flow_def);
    uref_flow_delete_headers(flow_def);
    upipe_avcdec_store_flow_def(upipe, flow_def);
}

/** @internal @This outputs subtitles.
 *
 * @param upipe description structure of the pipe
 * @param subtitle AVSubtitle subtitle
 * @param upump_p reference to upump structure
 */
static void upipe_avcdec_output_sub(struct upipe *upipe, AVSubtitle *sub,
                                    struct upump **upump_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    struct uref *uref = upipe_avcdec->uref;
    int width = upipe_avcdec->context->width;
    int height = upipe_avcdec->context->height;

#ifdef USE_COPY_OPAQUE
    uref = uref_dup(av_buffer_get_opaque(upipe_avcdec->avpkt->opaque_ref));
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
#endif

    if (!width || !height) {
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return;
    }

    const int alignment = 16;
    if (unlikely(upipe_avcdec->ubuf_mgr == NULL)) {
        /* Prepare flow definition attributes. */
        struct uref *flow_def_attr = upipe_avcdec_alloc_flow_def_attr(upipe);
        if (unlikely(flow_def_attr == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        if (unlikely(
#ifdef UPIPE_WORDS_BIGENDIAN
                !ubase_check(uref_pic_flow_set_argb(flow_def_attr)) ||
#else
                !ubase_check(uref_pic_flow_set_bgra(flow_def_attr)) ||
#endif
                !ubase_check(uref_flow_set_def(flow_def_attr, UREF_PIC_SUB_FLOW_DEF)) ||
                !ubase_check(uref_pic_set_progressive(flow_def_attr)) ||
                !ubase_check(uref_pic_flow_set_full_range(flow_def_attr))))
        {
            uref_free(flow_def_attr);
            upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
            return;
        }

        if (unlikely(!upipe_avcdec_demand_ubuf_mgr(upipe, flow_def_attr)))
            return;
    }

    struct uref *flow_def = uref_dup(upipe_avcdec->flow_def_provided);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return;
    }
    if (unlikely(
            !ubase_check(uref_pic_flow_set_align(flow_def, alignment)) ||
            !ubase_check(uref_pic_flow_set_hsize(flow_def, width)) ||
            !ubase_check(uref_pic_flow_set_vsize(flow_def, height)) ||
            !ubase_check(uref_pic_flow_set_hsize_visible(flow_def, width)) ||
            !ubase_check(uref_pic_flow_set_vsize_visible(flow_def, height))
            )) {
        upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
        return;
    }

    const char *input_def = NULL;
    uref_flow_get_def(upipe_avcdec->flow_def_input, &input_def);
    uref_pic_flow_set_sar(flow_def, (struct urational){ .num = 0, .den = 0});
    if (input_def && strstr(input_def, ".dvb_subtitle.")) {
        uint8_t subtype = 0;
        uref_ts_flow_get_sub_type(upipe_avcdec->flow_def_input, &subtype, 0);
        switch (subtype) {
            case 0x10:
            case 0x20:
            default:
                /* no monitor aspect ratio */
                break;
            case 0x11:
            case 0x21: {
                struct urational dar = { .num = 4, .den = 3 };
                uref_pic_flow_set_dar(flow_def, dar);
                struct urational sar = { .num = 1, .den = 1 };
                uref_pic_flow_set_sar(flow_def, sar);
                break;
            }
            case 0x13:
            case 0x23: {
                struct urational dar = { .num = 221, .den = 100 };
                uref_pic_flow_set_dar(flow_def, dar);
                struct urational sar = { .num = 1, .den = 1 };
                uref_pic_flow_set_sar(flow_def, sar);
                break;
            }
            case 0x12:
            case 0x14:
            case 0x15:
            case 0x16:
            case 0x22:
            case 0x24:
            case 0x25:
            case 0x26: {
                struct urational dar = { .num = 16, .den = 9 };
                uref_pic_flow_set_dar(flow_def, dar);
                struct urational sar = { .num = 1, .den = 1 };
                uref_pic_flow_set_sar(flow_def, sar);
                break;
            }
        }
    }

    /* Allocate a ubuf */
    struct ubuf *ubuf = ubuf_pic_alloc(upipe_avcdec->ubuf_mgr, width, height);
    if (unlikely(ubuf == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uref_pic_set_progressive(uref);
    ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1);

    uref_attach_ubuf(uref, ubuf);

    if (sub->end_display_time != UINT32_MAX)
        uref_clock_set_duration(uref, UCLOCK_FREQ * sub->end_display_time / 1000);

    uint64_t prog;
    int type;
    uref_clock_get_date_prog(uref, &prog, &type);

    uref_clock_set_date_prog(uref,
            prog + UCLOCK_FREQ * sub->start_display_time / 1000, type);

    if (sub->num_rects) {
        uint8_t *buf;
        const char *chroma;
        if (unlikely(!ubase_check(uref_pic_flow_get_chroma(flow_def,
                            &chroma, 0)) ||
                    !ubase_check(ubuf_pic_plane_write(uref->ubuf, chroma,
                            0, 0, -1, -1, &buf)))) {
            goto alloc_error;
        }

        /* Decode palettized to bgra */
        for (int i = 0; i < sub->num_rects; i++) {
            AVSubtitleRect *r = sub->rects[i];
            int x = r->x, y = r->y, w = r->w, h = r->h;

            if (x >= width) {
                upipe_warn_va(upipe, "x (%i/%i) is out of the picture",
                              x, width);
                continue;
            }
            if (y >= height) {
                upipe_warn_va(upipe, "y (%i/%i) is out of the picture",
                              y, height);
                continue;
            }

            if (x + w > width) {
                upipe_warn_va(upipe, "crop subtitle width %i -> %i",
                              w, width - x);
                w = width - x;
            }
            if (y + h > height) {
                upipe_warn_va(upipe, "crop subtitle height %i -> %i",
                              h, height - y);
                h = height - y;
            }

            uint8_t *dst = buf + 4 * ((width * y) + x);
            uint8_t *src = r->data[0];
            uint8_t *palette = r->data[1];

            for (int i = 0; i < h; i++) {
                for (int j = 0; j < w; j++) {
                    uint8_t idx = src[j];
                    if (unlikely(idx >= r->nb_colors)) {
                        upipe_err_va(upipe, "Invalid palette index %" PRIu8, idx);
                        continue;
                    }

                    memcpy(&dst[j*4], &palette[idx*4], 4);
                }

                dst += width * 4;
                src += w;
            }
        }

        ubuf_pic_plane_unmap(uref->ubuf, chroma, 0, 0, -1, -1);
    }

    /* Find out if flow def attributes have changed. */
    if (!upipe_avcdec_check_flow_def_attr(upipe, flow_def))
        upipe_avcdec_build_flow_def_sub(upipe, flow_def);
    else
        uref_free(flow_def);

    upipe_avcdec->uref = NULL;

    upipe_avcdec_output(upipe, uref, upump_p);
    return;

alloc_error:
    upipe_avcdec->uref = NULL;
    uref_free(uref);
    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    return;
}

/** @internal @This outputs video frames.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_avcdec_output_pic(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;
    AVFrame *frame = upipe_avcdec->frame;
    AVFrameSideData *side_data;

    if (frame->opaque_ref == NULL)
        return;

    struct uref *uref = av_buffer_get_opaque(frame->opaque_ref);
    struct uref *flow_def_attr = uref_from_uchain(uref->uchain.next);

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_verbose_va(upipe, "%"PRIu64"\t - Picture decoded ! %dx%d - %"PRIu64,
                 upipe_avcdec->counter, frame->width, frame->height, framenum);

    /* allocate new ubuf with wrapped avframe */
    bool use_ubuf_av = uref->ubuf == NULL;
    if (use_ubuf_av) {
        int ret;
        if (unlikely((ret = upipe_avcdec_get_buffer_pic(context, frame, 0)) < 0)) {
            upipe_err_va(upipe, "couldn't get frame buffer: %s", av_err2str(ret));
            upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
            return;
        }
    }

    flow_def_attr = uref_from_uchain(uref->uchain.next);
    /* Duplicate uref because it is freed in _release, because the ubuf
     * is still in use by avcodec. */
    struct uref *old_uref = uref;
    uref = uref_dup(uref);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (use_ubuf_av)
        ubuf_free(uref_detach_ubuf(old_uref));

    /* Resize the picture (was allocated too big). */
    if (unlikely(!ubase_check(uref_pic_resize(uref, 0, 0, frame->width, frame->height)))) {
        upipe_warn_va(upipe, "couldn't resize picture to %dx%d",
                      frame->width, frame->height);
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
    }

    UBASE_FATAL(upipe, uref_pic_set_tf(uref))
    UBASE_FATAL(upipe, uref_pic_set_bf(uref))
    if (!frame->interlaced_frame)
        UBASE_FATAL(upipe, uref_pic_set_progressive(uref))
    else if (frame->top_field_first)
        UBASE_FATAL(upipe, uref_pic_set_tff(uref))

    uint64_t duration = 0;
    AVRational uclock_time_base = av_make_q(1, UCLOCK_FREQ);

    if (context->framerate.num && context->framerate.den) {
        AVRational time_base = av_inv_q(context->framerate);
        duration = av_rescale_q(1, time_base, uclock_time_base);
    }

    if (!duration && frame->time_base.den)
        duration = (uint64_t)(2 + frame->repeat_pict) *
            context->ticks_per_frame * UCLOCK_FREQ * frame->time_base.num /
            (2 * frame->time_base.den);

    if (duration)
        UBASE_FATAL(upipe, uref_clock_set_duration(uref, duration))

    if (frame->key_frame)
        uref_pic_set_key(uref);

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_AFD);
    if (side_data && side_data->size == 1)
        uref_pic_set_afd(uref, side_data->data[0]);

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC);
    if (side_data)
        uref_pic_set_cea_708(uref, side_data->data, side_data->size);

    /* various time-related attributes */
    upipe_avcdec_set_time_attributes(upipe, uref);

    uref_h26x_delete_nal_offsets(uref);

    /* Find out if flow def attributes have changed. */
    if (!upipe_avcdec_check_flow_def_attr(upipe, flow_def_attr)) {
        /* Make a copy as flow_def_attr is still used by _release. */
        flow_def_attr = uref_dup(flow_def_attr);
        if (unlikely(flow_def_attr == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        struct uref *flow_def =
            upipe_avcdec_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_block_flow_clear_format(flow_def);
            uref_flow_delete_headers(flow_def);
            upipe_avcdec_store_flow_def(upipe, flow_def);
        }
    }

    upipe_avcdec_output(upipe, uref, upump_p);
}

/** @internal @This outputs audio buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_avcdec_output_sound(struct upipe *upipe,
                                      struct upump **upump_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    AVCodecContext *context = upipe_avcdec->context;
    AVFrame *frame = upipe_avcdec->frame;
    struct uref *uref = av_buffer_get_opaque(frame->opaque_ref);
    struct uref *flow_def_attr = uref_from_uchain(uref->uchain.next);

    uint64_t framenum = 0;
    uref_pic_get_number(uref, &framenum);

    upipe_verbose_va(upipe, "%"PRIu64"\t - Frame decoded ! %"PRIu64" (%p)",
                     upipe_avcdec->counter, framenum, uref);

    /* allocate new ubuf with wrapped avframe */
    bool use_ubuf_av = uref->ubuf == NULL;
    if (use_ubuf_av) {
        int ret;
        if (unlikely((ret = upipe_avcdec_get_buffer_sound(context, frame, 0)) < 0)) {
            upipe_err_va(upipe, "couldn't get frame buffer: %s", av_err2str(ret));
            upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
            return;
        }
    }

    flow_def_attr = uref_from_uchain(uref->uchain.next);
    /* Duplicate uref because it is freed in _release, because the ubuf
     * is still in use by avcodec. */
    struct uref *old_uref = uref;
    uref = uref_dup(uref);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (use_ubuf_av)
        ubuf_free(uref_detach_ubuf(old_uref));

    /* In case it has been reduced. */
    uref_sound_resize(uref, 0, frame->nb_samples);

    /* samples in uref */
    UBASE_FATAL(upipe, uref_sound_flow_set_samples(uref, frame->nb_samples))
    if (context->sample_rate)
        UBASE_FATAL(upipe, uref_clock_set_duration(uref,
                                (uint64_t)frame->nb_samples * UCLOCK_FREQ /
                                context->sample_rate));

    /* various time-related attribute */
    upipe_avcdec_set_time_attributes(upipe, uref);

    /* sample_rate can only be retrieved here */
    UBASE_FATAL(upipe, uref_sound_flow_set_rate(flow_def_attr, context->sample_rate))

    /* Find out if flow def attributes have changed. */
    if (!upipe_avcdec_check_flow_def_attr(upipe, flow_def_attr)) {
        /* Make a copy as flow_def_attr is still used by _release. */
        flow_def_attr = uref_dup(flow_def_attr);
        if (unlikely(flow_def_attr == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        struct uref *flow_def =
            upipe_avcdec_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_block_flow_clear_format(flow_def);
            upipe_avcdec_store_flow_def(upipe, flow_def);
        }
    }

    upipe_avcdec_output(upipe, uref, upump_p);
}

/** @internal @This decodes av packets.
 *
 * @param upipe description structure of the pipe
 * @param avpkt av packet
 * @param upump_p reference to upump structure
 * @return true if a frame was output
 */
static bool upipe_avcdec_decode_avpkt(struct upipe *upipe, AVPacket *avpkt,
                                      struct upump **upump_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    int gotframe = 0, len, err;
    AVCodecContext *context = upipe_avcdec->context;

    switch (context->codec->type) {
        case AVMEDIA_TYPE_SUBTITLE: {
            AVSubtitle subtitle;
            if (avpkt == NULL)
                avpkt = upipe_avcdec->avpkt;
            /* store original pointer */
            void *data = avpkt->data;

            if (context->codec_id == AV_CODEC_ID_DVB_SUBTITLE
                    && avpkt->size >= DVBSUB_HEADER_SIZE) {
                /* skip header, avcodec doesn't know to do it */
                avpkt->data += DVBSUB_HEADER_SIZE;
                avpkt->size -= DVBSUB_HEADER_SIZE;
            }
            len = avcodec_decode_subtitle2(context,
                    &subtitle, &gotframe, avpkt);
            if (context->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
                /* restore original pointer */
                avpkt->data = data;
            }
            if (len < 0)
                upipe_warn(upipe, "Error while decoding subtitle");

            if (gotframe) {
                upipe_avcdec_output_sub(upipe, &subtitle, upump_p);
                avsubtitle_free(&subtitle);
            }
            break;
        }

        case AVMEDIA_TYPE_VIDEO:
        case AVMEDIA_TYPE_AUDIO:
            err = avcodec_send_packet(context, avpkt);
            if (err) {
                upipe_err_va(upipe, "avcodec_send_packet: %s",
                             av_err2str(err));
                break;
            }

            while (1) {
                err = avcodec_receive_frame(context, upipe_avcdec->frame);
                if (unlikely(err)) {
                    if (err != AVERROR(EAGAIN) &&
                        err != AVERROR_EOF)
                        upipe_err_va(upipe, "avcodec_receive_frame: %s",
                                     av_err2str(err));
                    break;
                }

                gotframe = 1;
                if (context->codec->type == AVMEDIA_TYPE_VIDEO)
                    upipe_avcdec_output_pic(upipe, upump_p);
                else if (context->codec->type == AVMEDIA_TYPE_AUDIO)
                    upipe_avcdec_output_sound(upipe, upump_p);
            }
            break;

        default:
            /* should never be here */
            upipe_err_va(upipe, "Unsupported media type (%d)",
                         context->codec->type);
            break;
    }
    return !!gotframe;
}

/** @internal @This stores a uref in the temporary structure (required for
 * buffer allocation in @ref upipe_avcdec_get_buffer).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_avcdec_store_uref(struct upipe *upipe, struct uref *uref)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

#ifdef USE_COPY_OPAQUE
    upipe_avcdec->avpkt->opaque_ref =
        av_buffer_create(NULL, 0, buffer_uref_free, uref, 0);
#else
    if (upipe_avcdec->uref != NULL && upipe_avcdec->uref->uchain.next != NULL)
        uref_free(uref_from_uchain(upipe_avcdec->uref->uchain.next));
    uref_free(upipe_avcdec->uref);
    upipe_avcdec->uref = uref;
#endif
}

/** @internal @This decodes packets.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return always true
 */
static bool upipe_avcdec_decode(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    assert(upipe);
    assert(uref);

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    size_t size = 0;
    uref_block_size(uref, &size);
    if (unlikely(!size)) {
        upipe_warn(upipe, "Received packet with size 0, dropping");
        uref_free(uref);
        return true;
    }

    AVPacket *avpkt = upipe_avcdec->avpkt;
    if (unlikely(av_new_packet(avpkt, size) < 0)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    uint64_t pts;
    uint64_t dts;
    uint64_t duration;
    if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
        avpkt->pts = pts;
    if (ubase_check(uref_clock_get_dts_prog(uref, &dts)))
        avpkt->dts = dts;
    if (ubase_check(uref_clock_get_duration(uref, &duration)))
        avpkt->duration = duration;

    upipe_verbose_va(upipe, "Received packet %"PRIu64" - size : %d",
                     upipe_avcdec->counter, avpkt->size);
    uref_block_extract(uref, 0, avpkt->size, avpkt->data);
    ubuf_free(uref_detach_ubuf(uref));

    uref_pic_set_number(uref, upipe_avcdec->counter++);
    uref_clock_get_rate(uref, &upipe_avcdec->drift_rate);
    uint64_t input_dts, input_dts_sys;
    if (ubase_check(uref_clock_get_dts_prog(uref, &input_dts)) &&
        ubase_check(uref_clock_get_dts_sys(uref, &input_dts_sys))) {
        upipe_avcdec->input_dts = input_dts;
        upipe_avcdec->input_dts_sys = input_dts_sys;
    }

    upipe_avcdec_store_uref(upipe, uref);
    upipe_avcdec_decode_avpkt(upipe, avpkt, upump_p);
    av_packet_unref(avpkt);

    return true;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avcdec_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    while (unlikely(!avcodec_is_open(upipe_avcdec->context))) {
        if (upipe_avcdec->upump_av_deal != NULL) {
            upipe_avcdec_hold_input(upipe, uref);
            upipe_avcdec_block_input(upipe, upump_p);
            return;
        }

        upipe_avcdec_open(upipe);
    }

    upipe_avcdec_decode(upipe, uref, upump_p);
}

/** @internal @This looks for a decoder with suitable hw support.
 *
 * @param upipe description structure of the pipe
 * @param codec_id codec id
 * @param hw_config return the selected hw config
 * @return the codec structure
 */
static const AVCodec *upipe_avcdec_find_codec(struct upipe *upipe,
                                              enum AVCodecID codec_id,
                                              const AVCodecHWConfig **hw_config)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    *hw_config = NULL;
    if (upipe_avcdec->hw_device_type == AV_HWDEVICE_TYPE_NONE)
        return avcodec_find_decoder(codec_id);

    const AVCodec *codec;
    void *i = NULL;
    while ((codec = av_codec_iterate(&i))) {
        if (!av_codec_is_decoder(codec) || codec->id != codec_id)
            continue;

        const AVCodecHWConfig *config;
        int j = 0;
        while ((config = avcodec_get_hw_config(codec, j++)))
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == upipe_avcdec->hw_device_type) {
                *hw_config = config;
                return codec;
            }
    }

    return NULL;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_avcdec_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    enum AVCodecID codec_id;
    const AVCodec *codec;
    const AVCodecHWConfig *hw_config;

    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF) ||
        !(codec_id = upipe_av_from_flow_def(def + strlen(EXPECTED_FLOW_DEF))) ||
        !(codec = upipe_avcdec_find_codec(upipe, codec_id, &hw_config)))) {
        upipe_err_va(upipe, "No decoder found for \"%s\"",
                def + strlen(EXPECTED_FLOW_DEF));
        return UBASE_ERR_INVALID;
    }

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    uint8_t *extradata_alloc = NULL;
    const uint8_t *extradata;
    size_t extradata_size = 0;
    if (ubase_check(uref_flow_get_headers(flow_def, &extradata, &extradata_size))) {
        extradata_alloc = malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (unlikely(extradata_alloc == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        memcpy(extradata_alloc, extradata, extradata_size);
        memset(extradata_alloc + extradata_size, 0,
               AV_INPUT_BUFFER_PADDING_SIZE);
    }

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_avcdec_alloc_flow_def_check(upipe, flow_def);
    if (unlikely(flow_def_check == NULL)) {
        free(extradata_alloc);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_check, def)) ||
                 (extradata_alloc != NULL &&
                  !ubase_check(uref_flow_set_headers(flow_def_check, extradata,
                                         extradata_size))))) {
        free(extradata_alloc);
        uref_free(flow_def_check);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    /* Select hw accel for this codec. */
    if (hw_config != NULL)
        upipe_avcdec->hw_pix_fmt = hw_config->pix_fmt;

    if (upipe_avcdec->context != NULL) {
        free(extradata_alloc);
        /* Die if the attributes changed. */
        /* NB: this supposes that all attributes are in the udict, and that
         * the udict is never empty. */
        if (!upipe_avcdec_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return UBASE_ERR_BUSY;
        }
        uref_free(flow_def_check);
    } else {
        if (unlikely((upipe_avcdec->context =
                         avcodec_alloc_context3(codec)) == NULL)) {
            free(extradata_alloc);
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_EXTERNAL);
            return UBASE_ERR_EXTERNAL;
        }

        upipe_avcdec->context->codec = codec;
        upipe_avcdec->context->opaque = upipe;
        if (extradata_alloc != NULL) {
            upipe_avcdec->context->extradata = extradata_alloc;
            upipe_avcdec->context->extradata_size = extradata_size;
        }

        const char *chroma[UPIPE_AV_MAX_PLANES];
        upipe_avcdec->context->pix_fmt =
            upipe_av_pixfmt_from_flow_def(flow_def, NULL, chroma);

        uint64_t width, height;
        if (ubase_check(uref_pic_flow_get_hsize(flow_def, &width)) &&
            ubase_check(uref_pic_flow_get_vsize(flow_def, &height))) {
            upipe_avcdec->context->width = width;
            upipe_avcdec->context->height = height;
        }

        upipe_avcdec->context->pkt_timebase.num = 1;
        upipe_avcdec->context->pkt_timebase.den = UCLOCK_FREQ;

        struct urational fps;
        if (ubase_check(uref_pic_flow_get_fps(flow_def, &fps))) {
            upipe_avcdec->context->framerate.num = fps.num;
            upipe_avcdec->context->framerate.den = fps.den;
        }

        upipe_avcdec_store_flow_def_check(upipe, flow_def_check);
    }
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_avcdec_store_flow_def_input(upipe, flow_def);
    if (flow_def != NULL)
        uref_free(flow_def);

    upipe_avcdec->input_latency = 0;
    uref_clock_get_latency(upipe_avcdec->flow_def_input,
                           &upipe_avcdec->input_latency);
    return UBASE_ERR_NONE;
}

/** @internal @This checks some option compatibility (kinda kludgy ...).
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL
 * @return false in case of error
 */
static bool upipe_avcdec_check_option(struct upipe *upipe, const char *option,
                                      const char *content)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (upipe_avcdec->context == NULL)
        return false;

    /* lowres */
    if (!strcmp(option, "lowres")) {
        if (!content) return true;
        uint8_t lowres = strtoul(content, NULL, 10);
        if (lowres > upipe_avcdec->context->codec->max_lowres) {
            return false;
        }
    }
    return true;
}

/** @internal @This sets the content of an avcodec option. It only take effect
 * after the next call to @ref upipe_avcdec_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_avcdec_set_option(struct upipe *upipe,
                                   const char *option, const char *content)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    if (upipe_avcdec->context == NULL || avcodec_is_open(upipe_avcdec->context))
        return UBASE_ERR_BUSY;
    assert(option != NULL);
    if (unlikely(!upipe_avcdec_check_option(upipe, option, content))) {
        upipe_err_va(upipe, "can't set option %s:%s", option, content);
        return UBASE_ERR_EXTERNAL;
    }
    int error = av_opt_set(upipe_avcdec->context, option, content,
                           AV_OPT_SEARCH_CHILDREN);
    if (unlikely(error < 0)) {
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     av_err2str(error));
        return UBASE_ERR_EXTERNAL;
    }
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
static int upipe_avcdec_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_avcdec_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_avcdec_free_output_proxy(upipe, request);
        }
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_avcdec_set_upump_av_deal(upipe, NULL);
            upipe_avcdec_abort_av_deal(upipe);
            return upipe_avcdec_attach_upump_mgr(upipe);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avcdec_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_avcdec_control_output(upipe, command, args);

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_avcdec_set_option(upipe, option, content);
        }

        case UPIPE_AVCDEC_SET_HW_CONFIG: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVCDEC_SIGNATURE)
            const char *device_type = va_arg(args, const char *);
            const char *device = va_arg(args, const char *);
            struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
            enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
            if (device_type != NULL) {
                type = av_hwdevice_find_type_by_name(device_type);
                if (type == AV_HWDEVICE_TYPE_NONE)
                    return UBASE_ERR_INVALID;
            }
            upipe_avcdec->hw_device_type = type;
            upipe_avcdec->hw_device = device ? strdup(device) : NULL;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcdec_free(struct upipe *upipe)
{
    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);

    if (upipe_avcdec->context != NULL) {
        free(upipe_avcdec->context->extradata);
        av_free(upipe_avcdec->context);
    }
    av_frame_free(&upipe_avcdec->frame);
    av_packet_free(&upipe_avcdec->avpkt);
    free(upipe_avcdec->hw_device);

    upipe_throw_dead(upipe);
    uref_free(upipe_avcdec->uref);
    uref_free(upipe_avcdec->flow_def_format);
    uref_free(upipe_avcdec->flow_def_provided);
    upipe_avcdec_abort_av_deal(upipe);
    upipe_avcdec_clean_input(upipe);
    upipe_avcdec_clean_output(upipe);
    upipe_avcdec_clean_flow_def(upipe);
    upipe_avcdec_clean_flow_def_check(upipe);
    upipe_avcdec_clean_ubuf_mgr(upipe);
    upipe_avcdec_clean_upump_av_deal(upipe);
    upipe_avcdec_clean_upump_mgr(upipe);
    upipe_avcdec_clean_urefcount(upipe);
    upipe_avcdec_free_void(upipe);
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
    AVFrame *frame = av_frame_alloc();
    if (unlikely(frame == NULL))
        return NULL;

    AVPacket *avpkt = av_packet_alloc();
    if (unlikely(avpkt == NULL)) {
        av_frame_free(&frame);
        return NULL;
    }

    struct upipe *upipe = upipe_avcdec_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL)) {
        av_frame_free(&frame);
        av_packet_free(&avpkt);
        return NULL;
    }
    upipe_avcdec_init_urefcount(upipe);
    upipe_avcdec_init_ubuf_mgr(upipe);
    upipe_avcdec_init_upump_mgr(upipe);
    upipe_avcdec_init_upump_av_deal(upipe);
    upipe_avcdec_init_output(upipe);
    upipe_avcdec_init_flow_def(upipe);
    upipe_avcdec_init_flow_def_check(upipe);
    upipe_avcdec_init_input(upipe);

    struct upipe_avcdec *upipe_avcdec = upipe_avcdec_from_upipe(upipe);
    upipe_avcdec->hw_device_type = AV_HWDEVICE_TYPE_NONE;
    upipe_avcdec->hw_device = NULL;
    upipe_avcdec->hw_pix_fmt = AV_PIX_FMT_NONE;
    upipe_avcdec->context = NULL;
    upipe_avcdec->frame = frame;
    upipe_avcdec->avpkt = avpkt;
    upipe_avcdec->counter = 0;
    upipe_avcdec->close = false;
    upipe_avcdec->pix_fmt = AV_PIX_FMT_NONE;
    upipe_avcdec->sample_fmt = AV_SAMPLE_FMT_NONE;
    upipe_avcdec->channels = 0;
    upipe_avcdec->uref = NULL;
    upipe_avcdec->flow_def_format = NULL;
    upipe_avcdec->flow_def_provided = NULL;

    upipe_avcdec->index_rap = 0;
    upipe_avcdec->iframe_rap = 0;
    upipe_avcdec->last_pts = UINT64_MAX;
    upipe_avcdec->last_pts_sys = UINT64_MAX;
    upipe_avcdec->next_pts = UINT64_MAX;
    upipe_avcdec->next_pts_sys = UINT64_MAX;
    upipe_avcdec->input_latency = 0;
    upipe_avcdec->drift_rate.num = upipe_avcdec->drift_rate.den = 1;
    upipe_avcdec->input_dts = UINT64_MAX;
    upipe_avcdec->input_dts_sys = UINT64_MAX;

    upipe_throw_ready(upipe);
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avcdec_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AVCDEC_SIGNATURE,

    .upipe_alloc = upipe_avcdec_alloc,
    .upipe_input = upipe_avcdec_input,
    .upipe_control = upipe_avcdec_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for avcodec decoders.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcdec_mgr_alloc(void)
{
    return &upipe_avcdec_mgr;
}
