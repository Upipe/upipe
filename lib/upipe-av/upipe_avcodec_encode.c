/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe avcodec encode module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_flow_def_check.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe/udict_dump.h>

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

#define PREFIX_FLOW "block."

UREF_ATTR_INT(avcenc, priv, "x.avcenc_priv", avcenc private pts)

/** start offset of avcodec PTS */
#define AVCPTS_INIT 1

/** @hidden */
static int upipe_avcenc_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static bool upipe_avcenc_encode_frame(struct upipe *upipe,
                                      struct AVFrame *frame,
                                      struct upump **upump_p);
/** @hidden */
static void upipe_avcenc_encode_audio(struct upipe *upipe,
                                      struct upump **upump_p);
/** @hidden */
static bool upipe_avcenc_encode(struct upipe *upipe,
                                struct uref *uref, struct upump **upump_p);

/** upipe_avcenc structure with avcenc parameters */ 
struct upipe_avcenc {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow */
    struct uref *flow_def_input;
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

    /** temporary uref storage (used for sound processing) */
    struct uchain sound_urefs;
    /** nb samples in storage */
    unsigned int nb_samples;

    /** uref associated to frames currently in encoder */
    struct uchain urefs_in_use;
    /** last incoming pts (in avcodec timebase) */
    int64_t avcpts;

    /** frame counter */
    uint64_t counter;
    /** latency in the input flow */
    uint64_t input_latency;
    /** chroma map */
    const char *chroma_map[UPIPE_AV_MAX_PLANES];

    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** true if the context will be closed */
    bool close;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avcenc, upipe, UPIPE_AVCENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_avcenc, urefcount, upipe_avcenc_close)
UPIPE_HELPER_FLOW(upipe_avcenc, "block.")
UPIPE_HELPER_OUTPUT(upipe_avcenc, output, flow_def, output_state, request_list)
UPIPE_HELPER_FLOW_DEF(upipe_avcenc, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_avcenc, flow_def_check)

UPIPE_HELPER_UBUF_MGR(upipe_avcenc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_avcenc_check,
                      upipe_avcenc_register_output_request,
                      upipe_avcenc_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_avcenc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcenc, upump_av_deal, upump_mgr)
UPIPE_HELPER_INPUT(upipe_avcenc, urefs, nb_urefs, max_urefs, blockers, upipe_avcenc_encode)

/** @hidden */
static void upipe_avcenc_free(struct upipe *upipe);

/** @internal @This provides a ubuf_mgr request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_avcenc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (flow_format != NULL) {
        uref_free(upipe_avcenc->flow_def_provided);
        upipe_avcenc->flow_def_provided = flow_format;
    }
    return UBASE_ERR_NONE;
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (unlikely(upipe_avcenc->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avcenc->upump_av_deal);
        upump_free(upipe_avcenc->upump_av_deal);
        upipe_avcenc->upump_av_deal = NULL;
    }
}

/** @internal @This actually calls avcodec_open(). It may only be called by
 * one thread at a time.
 *
 * @param upipe description structure of the pipe
 * @return false if the buffers mustn't be dequeued
 */
static bool upipe_avcenc_do_av_deal(struct upipe *upipe)
{
    assert(upipe);
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;

    if (upipe_avcenc->close) {
        upipe_notice_va(upipe, "codec %s (%s) %d closed", context->codec->name, 
                        context->codec->long_name, context->codec->id);
        avcodec_close(context);
        return false;
    }

    /* open new context */
    int err;
    if (unlikely((err = avcodec_open2(context, context->codec, NULL)) < 0)) {
        upipe_av_strerror(err, buf);
        upipe_warn_va(upipe, "could not open codec (%s)", buf);
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
static void upipe_avcenc_cb_av_deal(struct upump *upump)
{
    assert(upump);
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);

    /* check udeal */
    if (unlikely(!upipe_av_deal_grab()))
        return;

    /* real open_codec function */
    bool ret = upipe_avcenc_do_av_deal(upipe);

    /* clean dealer */
    upipe_av_deal_yield(upump);
    upump_free(upipe_avcenc->upump_av_deal);
    upipe_avcenc->upump_av_deal = NULL;

    if (upipe_avcenc->close) {
        upipe_avcenc_free(upipe);
        return;
    }

    if (ret) 
        upipe_avcenc_output_input(upipe);
    else
        upipe_avcenc_flush_input(upipe);
    upipe_avcenc_unblock_input(upipe);
    /* All packets have been output, release again the pipe that has been
     * used in @ref upipe_avcenc_start_av_deal. */
    upipe_release(upipe);
}

/** @internal @This is called to trigger avcodec_open() or avcodec_close().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_start_av_deal(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    /* abort a pending open request */
    upipe_avcenc_abort_av_deal(upipe);

    /* use udeal/upump callback if available */
    upipe_avcenc_check_upump_mgr(upipe);
    if (upipe_avcenc->upump_mgr == NULL) {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcodec_open");
        upipe_avcenc_do_av_deal(upipe);
        if (upipe_avcenc->close)
            upipe_avcenc_free(upipe);
        return;
    }

    upipe_dbg(upipe, "upump_mgr present, using udeal");
    struct upump *upump_av_deal =
        upipe_av_deal_upump_alloc(upipe_avcenc->upump_mgr,
                                  upipe_avcenc_cb_av_deal, upipe);
    if (unlikely(!upump_av_deal)) {
        upipe_err(upipe, "can't create dealer");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_avcenc->upump_av_deal = upump_av_deal;
    /* Increment upipe refcount to avoid disappearing before all packets
     * have been sent. */
    upipe_use(upipe);
    upipe_av_deal_start(upump_av_deal);
}

/** @internal @This is called to trigger avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_open(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    upipe_avcenc->close = false;
    upipe_avcenc_start_av_deal(upipe);
}

/** @internal @This is called to trigger avcodec_close().
 *
 * We close the context even if it was not opened because it supposedly
 * "frees allocated structures".
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_close(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    if ((context == NULL) || !avcodec_is_open(context)) {
        upipe_avcenc_free(upipe);
        return;
    }

    if (avcodec_is_open(context)) {
        if (!ulist_empty(&upipe_avcenc->sound_urefs))
            /* Feed avcodec with the last incomplete uref (sound only). */
            upipe_avcenc_encode_audio(upipe, NULL);

        if (context->codec->capabilities & CODEC_CAP_DELAY) {
            /* Feed avcodec with NULL frames to output the remaining packets. */
            while (upipe_avcenc_encode_frame(upipe, NULL, NULL));
        }
    }
    upipe_avcenc->close = true;
    upipe_avcenc_start_av_deal(upipe);
}

/** @internal @This encodes av frames.
 *
 * @param upipe description structure of the pipe
 * @param frame frame
 * @param upump_p reference to upump structure
 * @return true when a packet has been output
 */
static bool upipe_avcenc_encode_frame(struct upipe *upipe,
                                      struct AVFrame *frame,
                                      struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    const AVCodec *codec = context->codec;

    if (unlikely(frame == NULL))
        upipe_dbg(upipe, "received null frame");

    /* encode frame */
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = NULL;
    avpkt.size = 0;
    int gotframe = 0;
    int err;
    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            err = avcodec_encode_video2(context, &avpkt, frame, &gotframe);
            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            err = avcodec_encode_audio2(context, &avpkt, frame, &gotframe);
            break;
        }
        default: /* should never be there */
            return false;
    }

    if (err < 0) {
        upipe_av_strerror(err, buf);
        upipe_warn_va(upipe, "error while encoding frame (%s)", buf);
        return false;
    }
    /* output encoded frame if available */
    if (!(gotframe && avpkt.data)) {
        return false;
    }

    /* flow definition */
    struct uref *flow_def_attr = upipe_avcenc_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def_attr == NULL)) {
        av_free_packet(&avpkt);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    if (context->extradata_size) {
        UBASE_FATAL(upipe, uref_flow_set_headers(flow_def_attr, context->extradata,
                    context->extradata_size))
    }

    const char *codec_def = upipe_av_to_flow_def(codec->id);
    UBASE_FATAL(upipe, uref_flow_set_def_va(flow_def_attr, PREFIX_FLOW "%s",
                                      codec_def));
    if (context->bit_rate) {
        uref_block_flow_set_octetrate(flow_def_attr, context->bit_rate / 8);
        if (context->rc_buffer_size)
            uref_block_flow_set_buffer_size(flow_def_attr,
                                            context->rc_buffer_size / 8);

        if (codec->type == AVMEDIA_TYPE_AUDIO && context->frame_size > 0) {
            uref_sound_flow_set_samples(flow_def_attr, context->frame_size);
        }
    }
    if (context->delay) {
        struct urational fps;
        uint64_t rate;
        if (ubase_check(uref_pic_flow_get_fps(upipe_avcenc->flow_def_input,
                                              &fps))) {
            UBASE_FATAL(upipe, uref_clock_set_latency(flow_def_attr,
                    upipe_avcenc->input_latency +
                    context->delay * UCLOCK_FREQ * fps.den / fps.num));
        } else if (ubase_check(uref_sound_flow_get_rate(
                        upipe_avcenc->flow_def_input, &rate))) {
            UBASE_FATAL(upipe, uref_clock_set_latency(flow_def_attr,
                    upipe_avcenc->input_latency +
                    context->delay * UCLOCK_FREQ / rate));
        }
    }

    if (unlikely(upipe_avcenc->ubuf_mgr == NULL)) {
        if (unlikely(!upipe_avcenc_demand_ubuf_mgr(upipe, flow_def_attr))) {
            av_free_packet(&avpkt);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }
    } else
        uref_free(flow_def_attr);

    flow_def_attr = uref_dup(upipe_avcenc->flow_def_provided);

    /* Find out if flow def attributes have changed. */
    if (!upipe_avcenc_check_flow_def_attr(upipe, flow_def_attr)) {
        struct uref *flow_def =
            upipe_avcenc_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_pic_flow_clear_format(flow_def);
            upipe_avcenc_store_flow_def(upipe, flow_def);
        }
    } else
        uref_free(flow_def_attr);

    struct ubuf *ubuf = ubuf_block_alloc(upipe_avcenc->ubuf_mgr, avpkt.size);
    if (unlikely(ubuf == NULL)) {
        av_free_packet(&avpkt);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    int size = -1;
    uint8_t *buf;
    if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &size, &buf)))) {
        ubuf_free(ubuf);
        av_free_packet(&avpkt);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    memcpy(buf, avpkt.data, size);
    ubuf_block_unmap(ubuf, 0);
    av_free_packet(&avpkt);

    /* find uref corresponding to avpkt */
    upipe_verbose_va(upipe, "output pts %"PRId64, avpkt.pts);
    struct uchain *uchain;
    struct uchain *uchain_tmp;
    struct uref *uref = NULL;
    ulist_delete_foreach (&upipe_avcenc->urefs_in_use, uchain, uchain_tmp) {
        struct uref *uref_chain = uref_from_uchain(uchain);
        int64_t priv = 0;
        if (ubase_check(uref_avcenc_get_priv(uref_chain, &priv)) && priv == avpkt.pts) {
            uref = uref_chain;
            ulist_delete(uchain);
            break;
        }
    }
    if (unlikely(uref == NULL)) {
        upipe_warn_va(upipe, "could not find pts %"PRId64" in urefs in use",
                      avpkt.pts);
        ubuf_free(ubuf);
        return false;
    }

    /* unmap input */
    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            int i;
            for (i = 0; i < UPIPE_AV_MAX_PLANES &&
                        upipe_avcenc->chroma_map[i] != NULL; i++)
                uref_pic_plane_unmap(uref, upipe_avcenc->chroma_map[i],
                                     0, 0, -1, -1);
            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            break;
        }
        default: /* should never be there */
            uref_free(uref);
            return false;
    }
    uref_attach_ubuf(uref, ubuf);
    uref_avcenc_delete_priv(uref);

    /* set dts */
    uint64_t dts_pts_delay = (uint64_t)(avpkt.pts - avpkt.dts) * UCLOCK_FREQ
                              * context->time_base.num
                              / context->time_base.den;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);
    uref_clock_delete_cr_dts_delay(uref);

    /* rebase to dts as we're in encoded domain now */
    uref_clock_rebase_dts_sys(uref);
    uref_clock_rebase_dts_prog(uref);
    uref_clock_rebase_dts_orig(uref);

    if (avpkt.flags & AV_PKT_FLAG_KEY)
        uref_flow_set_random(uref);

    upipe_avcenc_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This encodes video frames.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avcenc_encode_video(struct upipe *upipe,
                                      struct uref *uref, struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    AVFrame *frame = upipe_avcenc->frame;

    /* FIXME check picture format against flow def */
    size_t hsize, vsize;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 hsize != context->width || vsize != context->height)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    int i;
    for (i = 0; i < UPIPE_AV_MAX_PLANES && upipe_avcenc->chroma_map[i] != NULL;
         i++) {
        const uint8_t *data;
        size_t stride;
        if (unlikely(!ubase_check(uref_pic_plane_read(uref, upipe_avcenc->chroma_map[i],
                                          0, 0, -1, -1, &data)) ||
                     !ubase_check(uref_pic_plane_size(uref, upipe_avcenc->chroma_map[i],
                                          &stride, NULL, NULL, NULL)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        frame->data[i] = (uint8_t *)data;
        frame->linesize[i] = stride;
    }

    /* set pts (needed for uref/avpkt mapping) */
    upipe_verbose_va(upipe, "input pts %"PRId64, upipe_avcenc->avcpts);
    frame->pts = upipe_avcenc->avcpts++;
    if (unlikely(!ubase_check(uref_avcenc_set_priv(uref, frame->pts)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* set aspect ratio if it was changed in the input */
    struct urational sar = {1, 1};
    if (ubase_check(uref_pic_flow_get_sar(upipe_avcenc->flow_def_input, &sar))) {
        context->sample_aspect_ratio.num = sar.num;
        context->sample_aspect_ratio.den = sar.den;
    }

    /* store uref in mapping list */
    ulist_add(&upipe_avcenc->urefs_in_use, uref_to_uchain(uref));
    upipe_avcenc_encode_frame(upipe, frame, upump_p);
}

/** @internal @This encodes audio frames.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_avcenc_encode_audio(struct upipe *upipe,
                                      struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    AVFrame *frame = upipe_avcenc->frame;

    int size = av_samples_get_buffer_size(NULL, context->channels,
                                          context->frame_size,
                                          context->sample_fmt, 0);

    frame->nb_samples = context->frame_size;
    frame->format = context->sample_fmt;
    frame->channel_layout = context->channel_layout;

    /* TODO replace with umem */
    uint8_t *buf = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(buf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    avcodec_fill_audio_frame(frame, context->channels,
                             context->sample_fmt, buf, size, 0);

    struct uref *main_uref = NULL;
    size_t offset = 0;
    while (offset < context->frame_size) {
        struct uchain *uchain = ulist_peek(&upipe_avcenc->sound_urefs);
        if (unlikely(uchain == NULL)) {
            /* end of stream, finish with silence */
            av_samples_set_silence(frame->data, offset,
                                   context->frame_size - offset,
                                   context->channels, context->sample_fmt);
            break;
        }

        struct uref *uref = uref_from_uchain(uchain);
        assert(uref != NULL);
        if (main_uref == NULL)
            main_uref = uref_dup(uref);
        size_t size;
        uref_sound_size(uref, &size, NULL);

        size_t extracted = (context->frame_size - offset) < size ?
                           (context->frame_size - offset) : size;
        const uint8_t *buffers[AV_NUM_DATA_POINTERS];
        if (unlikely(!ubase_check(uref_sound_read_uint8_t(uref, 0, extracted,
                                        buffers, AV_NUM_DATA_POINTERS)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref_from_uchain(ulist_pop(&upipe_avcenc->sound_urefs)));
            upipe_avcenc->nb_samples -= size;
            free(buf);
            return;
        }
        int err = av_samples_copy(frame->data, buffers, offset, 0, extracted,
                                  context->channels, context->sample_fmt);
        UBASE_ERROR(upipe, uref_sound_unmap(uref, 0, extracted,
                                            AV_NUM_DATA_POINTERS))
        if (err < 0)
            upipe_warn_va(upipe, "av_samples_copy error %d", err);

        offset += extracted;
        upipe_avcenc->nb_samples -= extracted;
        if (extracted == size)
            uref_free(uref_from_uchain(ulist_pop(&upipe_avcenc->sound_urefs)));
        else {
            uref_sound_resize(uref, extracted, -1);
            uint64_t duration = (uint64_t)extracted * UCLOCK_FREQ /
                                context->sample_rate;
            uint64_t pts;
            if (ubase_check(uref_clock_get_pts_prog(uref, &pts)))
                uref_clock_set_pts_prog(uref, pts + duration);
            if (ubase_check(uref_clock_get_pts_sys(uref, &pts)))
                uref_clock_set_pts_sys(uref, pts + duration);
            if (ubase_check(uref_clock_get_pts_orig(uref, &pts)))
                uref_clock_set_pts_orig(uref, pts + duration);
        }
    }

    /* set pts (needed for uref/avpkt mapping) */
    upipe_verbose_va(upipe, "input pts %"PRId64, upipe_avcenc->avcpts);
    frame->pts = upipe_avcenc->avcpts++;
    if (unlikely(!ubase_check(uref_avcenc_set_priv(main_uref, frame->pts)))) {
        uref_free(main_uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        free(buf);
        return;
    }

    /* store uref in mapping list */
    ulist_add(&upipe_avcenc->urefs_in_use, uref_to_uchain(main_uref));
    upipe_avcenc_encode_frame(upipe, frame, upump_p);
    free(buf);
}

/** @internal @This encodes frames.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return always true
 */
static bool upipe_avcenc_encode(struct upipe *upipe,
                                struct uref *uref, struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;

    /* map input */
    switch (context->codec->type) {
        case AVMEDIA_TYPE_VIDEO:
            upipe_avcenc_encode_video(upipe, uref, upump_p);
            break;

        case AVMEDIA_TYPE_AUDIO: {
            size_t size;
            if (unlikely(!ubase_check(uref_sound_size(uref, &size, NULL)))) {
                upipe_warn(upipe, "invalid uref received");
                uref_free(uref);
                return true;
            }

            ulist_add(&upipe_avcenc->sound_urefs, uref_to_uchain(uref));
            upipe_avcenc->nb_samples += size;

            while (upipe_avcenc->nb_samples >= context->frame_size)
                upipe_avcenc_encode_audio(upipe, upump_p);
            break;
        }
        default:
            uref_free(uref);
            break;
    }
    return true;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avcenc_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);

    while (unlikely(!avcodec_is_open(upipe_avcenc->context))) {
        if (upipe_avcenc->upump_av_deal != NULL) {
            upipe_avcenc_hold_input(upipe, uref);
            upipe_avcenc_block_input(upipe, upump_p);
            return;
        }

        upipe_avcenc_open(upipe);
    }

    upipe_avcenc_encode(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_avcenc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (unlikely((ubase_ncmp(def, "pic.") && ubase_ncmp(def, "sound.")))) {
        upipe_err(upipe, "incompatible flow def");
        return UBASE_ERR_INVALID;
    }

    /* Extract relevant attributes to flow def check. */
    struct uref *flow_def_check =
        upipe_avcenc_alloc_flow_def_check(upipe, flow_def);
    if (unlikely(flow_def_check == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_check, def)))) {
        uref_free(flow_def_check);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (!ubase_ncmp(def, "pic.")) {
        uint64_t hsize, vsize;
        struct urational fps;
        if (!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)) ||
            !ubase_check(uref_pic_flow_get_fps(flow_def, &fps))) {
            upipe_err(upipe, "incompatible flow def attributes");
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        uref_pic_flow_get_hsize_visible(flow_def, &hsize);
        uref_pic_flow_get_vsize_visible(flow_def, &vsize);

        if (unlikely(!ubase_check(uref_pic_flow_copy_format(flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_set_hsize(flow_def_check, hsize)) ||
                     !ubase_check(uref_pic_flow_set_vsize(flow_def_check, vsize)) ||
                     !ubase_check(uref_pic_flow_set_fps(flow_def_check, fps)))) {
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else {
        uint8_t channels;
        uint64_t rate;
        if (!ubase_check(uref_sound_flow_get_channels(flow_def, &channels)) ||
            !ubase_check(uref_sound_flow_get_rate(flow_def, &rate))) {
            upipe_err(upipe, "incompatible flow def attributes");
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }

        if (unlikely(!ubase_check(uref_sound_flow_copy_format(flow_def_check, flow_def)) ||
                     !ubase_check(uref_sound_flow_set_channels(flow_def_check, channels)) ||
                     !ubase_check(uref_sound_flow_set_rate(flow_def_check, rate)))) {
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    }

    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    const AVCodec *codec = context->codec;

    if (avcodec_is_open(upipe_avcenc->context)) {
        /* Die if the attributes changed. */
        /* NB: this supposes that all attributes are in the udict, and that
         * the udict is never empty. */
        if (!upipe_avcenc_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return UBASE_ERR_BUSY;
        }
        uref_free(flow_def_check);

    } else if (!ubase_ncmp(def, "pic.")) {
        context->pix_fmt = upipe_av_pixfmt_from_flow_def(flow_def,
                    codec->pix_fmts, upipe_avcenc->chroma_map);
        if (context->pix_fmt == AV_PIX_FMT_NONE) {
            upipe_err_va(upipe, "unsupported pixel format");
            uref_dump(flow_def, upipe->uprobe);
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        upipe_avcenc->frame->format = context->pix_fmt;

        const AVRational *supported_framerates = codec->supported_framerates;
        struct urational fps = {25, 1};
        if (ubase_check(uref_pic_flow_get_fps(flow_def, &fps)) &&
            supported_framerates != NULL) {
            int i;
            for (i = 0; supported_framerates[i].num; i++)
                if (supported_framerates[i].num == fps.num &&
                    supported_framerates[i].den == fps.den)
                    break;
            if (!supported_framerates[i].num) {
                upipe_err_va(upipe, "unsupported frame rate %"PRIu64"/%"PRIu64,
                             fps.num, fps.den);
                uref_free(flow_def_check);
                return UBASE_ERR_INVALID;
            }
        }
        context->time_base.num = fps.den;
        context->time_base.den = fps.num;

        struct urational sar;
        if (ubase_check(uref_pic_flow_get_sar(flow_def, &sar))) {
            context->sample_aspect_ratio.num = sar.num;
            context->sample_aspect_ratio.den = sar.den;
        }

        uint64_t hsize = 0, vsize = 0;
        uref_pic_flow_get_hsize(flow_def, &hsize);
        uref_pic_flow_get_vsize(flow_def, &vsize);
        context->width = hsize;
        context->height = vsize;

        if (!ubase_check(uref_pic_get_progressive(flow_def))) {
            context->flags |= CODEC_FLAG_INTERLACED_DCT |
                              CODEC_FLAG_INTERLACED_ME;
        }

        upipe_avcenc_store_flow_def_check(upipe, flow_def_check);

    } else {
        uint8_t channels = 0;
        enum AVSampleFormat sample_fmt =
            upipe_av_samplefmt_from_flow_def(flow_def, &channels);
        const enum AVSampleFormat *sample_fmts = codec->sample_fmts;
        if (sample_fmt == AV_SAMPLE_FMT_NONE || sample_fmts == NULL) {
            upipe_err_va(upipe, "unknown sample format %s", def);
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        while (*sample_fmts != -1) {
            if (*sample_fmts == sample_fmt)
                break;
            sample_fmts++;
        }
        if (*sample_fmts == -1) {
            upipe_err_va(upipe, "unsupported sample format %s", def);
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        context->sample_fmt = *sample_fmts;

        uint64_t rate;
        const int *supported_samplerates = codec->supported_samplerates;
        if (!ubase_check(uref_sound_flow_get_rate(flow_def, &rate))) {
            upipe_err_va(upipe, "unsupported sample rate");
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        if (supported_samplerates != NULL) {
            while (*supported_samplerates != 0) {
                if (*supported_samplerates == rate)
                    break;
                supported_samplerates++;
            }
            if (*supported_samplerates == 0) {
                upipe_err_va(upipe, "unsupported sample rate %"PRIu64, rate);
                uref_free(flow_def_check);
                return UBASE_ERR_INVALID;
            }
        }
        context->sample_rate = rate;
        context->time_base.num = 1;
        context->time_base.den = 1;//rate; FIXME

        const uint64_t *channel_layouts =
            upipe_avcenc->context->codec->channel_layouts;
        while (*channel_layouts != 0) {
            if (av_get_channel_layout_nb_channels(*channel_layouts) == channels)
                break;
            channel_layouts++;
        }
        if (*channel_layouts == 0) {
            upipe_err_va(upipe, "unsupported channel layout %"PRIu8, channels);
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        context->channels = channels;
        context->channel_layout = *channel_layouts;

        upipe_avcenc_store_flow_def_check(upipe, flow_def_check);
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    flow_def = upipe_avcenc_store_flow_def_input(upipe, flow_def);
    if (flow_def != NULL) {
        uref_pic_flow_clear_format(flow_def);
        upipe_avcenc_store_flow_def(upipe, flow_def);
    }

    upipe_avcenc->input_latency = 0;
    uref_clock_get_latency(upipe_avcenc->flow_def_input,
                           &upipe_avcenc->input_latency);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_avcenc_provide_flow_format(struct upipe *upipe,
                                            struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    if (unlikely(context == NULL))
        goto upipe_avcenc_provide_flow_format_err;
    const AVCodec *codec = context->codec;
    if (unlikely(codec == NULL))
        goto upipe_avcenc_provide_flow_format_err;

    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_format, &def))))
        goto upipe_avcenc_provide_flow_format_err;
    if (!ubase_ncmp(def, "pic.")) {
        if (unlikely(codec->pix_fmts == NULL || codec->pix_fmts[0] == -1))
            goto upipe_avcenc_provide_flow_format_err;

        const char *chroma_map[UPIPE_AV_MAX_PLANES];
        enum AVPixelFormat pix_fmt = upipe_av_pixfmt_from_flow_def(flow_format,
                    codec->pix_fmts, chroma_map);
        if (pix_fmt == AV_PIX_FMT_NONE) {
            uref_pic_flow_clear_format(flow_format);
            if (unlikely(!ubase_check(upipe_av_pixfmt_to_flow_def(
                                codec->pix_fmts[0], flow_format))))
                goto upipe_avcenc_provide_flow_format_err;
        }

        const AVRational *supported_framerates = codec->supported_framerates;
        struct urational fps = {25, 1};
        if (ubase_check(uref_pic_flow_get_fps(flow_format, &fps)) &&
            supported_framerates != NULL) {
            int i;
            int closest = -1;
            float wanted_fps = (float)fps.num / (float)fps.den;
            float diff_fps = UINT16_MAX; /* arbitrarily big */
            for (i = 0; supported_framerates[i].num; i++) {
                if (supported_framerates[i].num == fps.num &&
                    supported_framerates[i].den == fps.den)
                    break;
                float this_fps = (float)supported_framerates[i].num /
                                 (float)supported_framerates[i].den;
                float this_diff_fps = this_fps > wanted_fps ?
                    this_fps - wanted_fps : wanted_fps - this_fps;
                if (this_diff_fps < diff_fps) {
                    diff_fps = this_diff_fps;
                    closest = i;
                }
            }
            if (!supported_framerates[i].num) {
                if (closest == -1)
                    goto upipe_avcenc_provide_flow_format_err;
                fps.num = supported_framerates[closest].num;
                fps.den = supported_framerates[closest].den;
                UBASE_RETURN(uref_pic_flow_set_fps(flow_format, fps))
            }
        }
        return urequest_provide_flow_format(request, flow_format);

    } else if (!ubase_ncmp(def, "sound.")) {
        uint8_t channels = 0;
        enum AVSampleFormat sample_fmt =
            upipe_av_samplefmt_from_flow_def(flow_format, &channels);
        const enum AVSampleFormat *sample_fmts = codec->sample_fmts;
        if (sample_fmt == AV_SAMPLE_FMT_NONE || sample_fmts == NULL)
            goto upipe_avcenc_provide_flow_format_err;

        while (*sample_fmts != -1) {
            if (*sample_fmts == sample_fmt)
                break;
            sample_fmts++;
        }
        if (*sample_fmts == -1)
            sample_fmt = codec->sample_fmts[0];

        uint64_t rate;
        const int *supported_samplerates = codec->supported_samplerates;
        if (!ubase_check(uref_sound_flow_get_rate(flow_format, &rate)))
            goto upipe_avcenc_provide_flow_format_err;

        if (supported_samplerates != NULL) {
            int i;
            int closest = -1;
            uint64_t diff_rate = UINT64_MAX; /* arbitrarily big */
            for (i = 0; supported_samplerates[i]; i++) {
                if (supported_samplerates[i] == rate)
                    break;
                uint64_t this_diff_rate = supported_samplerates[i] > rate ?
                                          supported_samplerates[i] - rate :
                                          rate - supported_samplerates[i];
                if (this_diff_rate < diff_rate) {
                    diff_rate = this_diff_rate;
                    closest = i;
                }
            }
            if (supported_samplerates[i] == 0) {
                if (closest == -1)
                    goto upipe_avcenc_provide_flow_format_err;
                UBASE_RETURN(uref_sound_flow_set_rate(flow_format,
                            codec->supported_samplerates[closest]))
            }
        }

        const uint64_t *channel_layouts = codec->channel_layouts;
        if (channel_layouts != NULL) {
            int i;
            int closest = -1;
            uint64_t diff_channels = UINT64_MAX; /* arbitrarily big */
            for (i = 0; av_get_channel_layout_nb_channels(channel_layouts[i]);
                 i++) {
                uint8_t this_channels =
                    av_get_channel_layout_nb_channels(channel_layouts[i]);
                if (this_channels == channels)
                    break;
                uint8_t this_diff_channels = this_channels > channels ?
                                             this_channels - channels :
                                             channels - this_channels;
                if (this_diff_channels < diff_channels) {
                    diff_channels = this_diff_channels;
                    closest = i;
                }
            }
            if (av_get_channel_layout_nb_channels(channel_layouts[i]) == 0) {
                if (closest == -1)
                    goto upipe_avcenc_provide_flow_format_err;
                channels =
                    av_get_channel_layout_nb_channels(channel_layouts[closest]);
            }
        }
        uref_sound_flow_clear_format(flow_format);
        uref_sound_flow_set_planes(flow_format, 0);
        upipe_av_samplefmt_to_flow_def(flow_format, sample_fmt, channels);
        return urequest_provide_flow_format(request, flow_format);

    }

upipe_avcenc_provide_flow_format_err:
    uref_flow_set_def(flow_format, "void.");
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This sets the content of an avcodec option. It only take effect
 * after the next call to @ref upipe_avcenc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_avcenc_set_option(struct upipe *upipe,
                                   const char *option, const char *content)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    assert(option != NULL);
    int error = av_opt_set(upipe_avcenc->context, option, content,
                           AV_OPT_SEARCH_CHILDREN);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
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
 * @return false in case of error
 */
static int upipe_avcenc_control(struct upipe *upipe,
                                           int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_throw_provide_request(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_avcenc_provide_flow_format(upipe, request);
            return upipe_avcenc_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_avcenc_free_output_proxy(upipe, request);
        }
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_avcenc_set_upump_av_deal(upipe, NULL);
            upipe_avcenc_abort_av_deal(upipe);
            return upipe_avcenc_attach_upump_mgr(upipe);

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_avcenc_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avcenc_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avcenc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_avcenc_set_output(upipe, output);
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_avcenc_set_option(upipe, option, content);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_free(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);

    if (upipe_avcenc->context != NULL)
        av_free(upipe_avcenc->context);
    av_free(upipe_avcenc->frame);

    /* free remaining urefs (should not be any) */
    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_avcenc->urefs_in_use)) != NULL) {
        struct uref *uref = uref_from_uchain(uchain);
        int64_t priv;
        if (ubase_check(uref_avcenc_get_priv(uref, &priv)))
            upipe_warn_va(upipe, "remaining uref %"PRId64" freed", priv);
        uref_free(uref);
    }

    upipe_throw_dead(upipe);
    uref_free(upipe_avcenc->flow_def_provided);
    upipe_avcenc_abort_av_deal(upipe);
    upipe_avcenc_clean_input(upipe);
    upipe_avcenc_clean_ubuf_mgr(upipe);
    upipe_avcenc_clean_upump_av_deal(upipe);
    upipe_avcenc_clean_upump_mgr(upipe);
    upipe_avcenc_clean_output(upipe);
    upipe_avcenc_clean_flow_def(upipe);
    upipe_avcenc_clean_flow_def_check(upipe);
    upipe_avcenc_clean_urefcount(upipe);
    upipe_avcenc_free_flow(upipe);
}

/** @internal @This allocates a avcenc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avcenc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    AVFrame *frame = avcodec_alloc_frame();
    if (unlikely(frame == NULL))
        return NULL;

    struct uref *flow_def;
    struct upipe *upipe = upipe_avcenc_alloc_flow(mgr, uprobe, signature, args,
                                                  &flow_def);
    if (unlikely(upipe == NULL)) {
        av_free(frame);
        return NULL;
    }

    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    const char *def, *name;
    enum AVCodecID codec_id;
    AVCodec *codec = NULL;

    if (!ubase_check(uref_avcenc_get_codec_name(flow_def, &name))
            || !(codec = avcodec_find_encoder_by_name(name))) {
        if (ubase_check(uref_flow_get_def(flow_def, &def)) &&
                (codec_id = upipe_av_from_flow_def(def + strlen("block.")))) {
            codec = avcodec_find_encoder(codec_id);
        }
    }
    
    if ((codec == NULL) ||
            (upipe_avcenc->context = avcodec_alloc_context3(codec)) == NULL) {
        uref_free(flow_def);
        av_free(frame);
        upipe_avcenc_free_flow(upipe);
        return NULL;
    }

    uref_free(flow_def);
    upipe_avcenc->frame = frame;
    upipe_avcenc->context->codec = codec;
    upipe_avcenc->context->opaque = upipe;

    upipe_avcenc_init_urefcount(upipe);
    upipe_avcenc_init_ubuf_mgr(upipe);
    upipe_avcenc_init_upump_mgr(upipe);
    upipe_avcenc_init_upump_av_deal(upipe);
    upipe_avcenc_init_output(upipe);
    upipe_avcenc_init_flow_def(upipe);
    upipe_avcenc_init_flow_def_check(upipe);
    upipe_avcenc_init_input(upipe);

    upipe_avcenc->flow_def_provided = NULL;
    ulist_init(&upipe_avcenc->sound_urefs);
    upipe_avcenc->nb_samples = 0;

    ulist_init(&upipe_avcenc->urefs_in_use);
    upipe_avcenc->avcpts = AVCPTS_INIT;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This configures the given flow definition to be able to encode to the
 * av codec described by name.
 *
 * @param flow_def flow definition packet
 * @param name codec name
 * @return an erorr code
 */
int _upipe_avcenc_mgr_set_flow_def_from_name(struct uref *flow_def,
                                             const char *name)
{
    if (name == NULL)
        return UBASE_ERR_INVALID;
    AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (codec == NULL)
        return UBASE_ERR_INVALID;
    const char *def = upipe_av_to_flow_def(codec->id);
    if (def == NULL)
        return UBASE_ERR_INVALID;
    return uref_flow_set_def(flow_def, def);
}

/** @This processes control commands on a avcenc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_avcenc_mgr_control(struct upipe_mgr *mgr,
                                    int command, va_list args)
{
    switch (command) {
        case UPIPE_AVCENC_MGR_SET_FLOW_DEF_FROM_NAME:
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVCENC_SIGNATURE)
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *name = va_arg(args, const char *);
            return _upipe_avcenc_mgr_set_flow_def_from_name(flow_def, name);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avcenc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AVCENC_SIGNATURE,

    .upipe_alloc = upipe_avcenc_alloc,
    .upipe_input = upipe_avcenc_input,
    .upipe_control = upipe_avcenc_control,

    .upipe_mgr_control = upipe_avcenc_mgr_control
};

/** @internal @This returns the management structure for avcodec encoders.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcenc_mgr_alloc(void)
{
    return &upipe_avcenc_mgr;
}
