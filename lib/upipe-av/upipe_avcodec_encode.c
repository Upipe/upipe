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
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_sink.h>
#include <upipe/upipe_helper_sound_stream.h>
#include <upipe-av/upipe_avcodec_encode.h>

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

/** BS for <= 2 channels */
#define AUDIO_BS 3584

UREF_ATTR_INT(avcenc, pts, "x.avcenc_pts", avcenc pts)

static bool upipe_avcenc_encode_frame(struct upipe *upipe,
                                      struct AVFrame *frame,
                                      struct upump *upump);
/** @internal @This handles incoming frames */
static bool upipe_avcenc_input_frame(struct upipe *upipe,
                                     struct uref *uref, struct upump *upump);


/** @internal @This are the parameters passed to avcodec_open2 by
 * upipe_avcodec_open_cb()
 */
struct upipe_avcodec_open_params {
    AVCodec *codec;
    AVDictionary *options;
    int width;
    int height;
};

/** upipe_avcenc structure with avcenc parameters */ 
struct upipe_avcenc {
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
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** upump mgr */
    struct upump_mgr *upump_mgr;

    /** upipe/av pixfmt translator */
    const struct upipe_av_pixfmt *pixfmt;
    /** last sample format */
    enum AVSampleFormat sample_fmt;

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

    /** uref associated to frames currently in encoder */
    struct uref **uref;
    /** urefs array size */
    int uref_max;
    /** last incoming pts (in avcodec timebase) */
    int64_t avcpts;

    /** frame counter */
    uint64_t counter;
    /** audio BS duration */
    uint64_t audio_bs_duration;
    /** audio BS leakage per frame */
    uint64_t audio_bs_leakage;
    /** audio BS delay */
    int64_t audio_bs_delay;

    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain stream_urefs;

    /** avcodec context */
    AVCodecContext *context;
    /** true if open_codec failed */
    bool open_failed;
    /** avcodec frame */
    AVFrame *frame;
    /** avcodec_open parameters */
    struct upipe_avcodec_open_params open_params;

    /** public upipe structure */
    struct upipe upipe;
};


UPIPE_HELPER_UPIPE(upipe_avcenc, upipe, UPIPE_AVCENC_SIGNATURE);
UPIPE_HELPER_FLOW(upipe_avcenc, NULL)
UPIPE_HELPER_UREF_MGR(upipe_avcenc, uref_mgr);
UPIPE_HELPER_OUTPUT(upipe_avcenc, output, output_flow, output_flow_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avcenc, ubuf_mgr);
UPIPE_HELPER_UPUMP_MGR(upipe_avcenc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcenc, upump_av_deal, upump_mgr)
UPIPE_HELPER_SINK(upipe_avcenc, urefs, nb_urefs, max_urefs, blockers, upipe_avcenc_input_frame)
UPIPE_HELPER_SOUND_STREAM(upipe_avcenc, next_uref, next_uref_size, stream_urefs)

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

/** @internal @This configures a new codec context
 *
 * @param upipe description structure of the pipe
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_avcenc_open_codec(struct upipe *upipe)
{
    AVCodecContext *context = NULL;
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcenc->open_params;
    enum PixelFormat pix_fmt;
    AVCodec *codec = params->codec;
    assert(upipe);
    int i;

    /* close previously opened context */
    if (unlikely(upipe_avcenc->context)) {
        /* first send empty packet to flush retained frames */
        upipe_dbg(upipe, "flushing frames in encoder");
        #if 0
        /* Send last incomplete audio chunk to the encoder */
        if (upipe_avcenc->context->codec->type == AVMEDIA_TYPE_AUDIO) {
            int size = av_samples_get_buffer_size(NULL,
                            upipe_avcenc->context->channels,
                            upipe_avcenc->context->frame_size,
                            upipe_avcenc->context->sample_fmt, 0);
            struct uref *uref = NULL;
            while (upipe_avcenc->next_uref) {
                uref = upipe_avcenc_extract_sound_stream(upipe, size,
                        upipe_avcenc->context->channels,
                        av_get_bytes_per_sample(upipe_avcenc->context->sample_fmt),
                        upipe_avcenc->context->sample_rate);
                upipe_avcenc_input_frame(upipe, uref, NULL);
            }
        }
        #endif
        if (upipe_avcenc->context->codec->capabilities & CODEC_CAP_DELAY) {
            while (upipe_avcenc_encode_frame(upipe, NULL, NULL));
        }

        /* now close codec */
        upipe_notice_va(upipe, "avcodec context (%s) closed (%d)",
                    upipe_avcenc->context->codec->name, upipe_avcenc->counter);
        avcodec_close(upipe_avcenc->context);
        av_free(upipe_avcenc->context);
        upipe_avcenc->context = NULL;
        upipe_avcenc_store_flow_def(upipe, NULL);

        /* free remaining urefs (should not be any) */
        if (upipe_avcenc->uref) {
            for (i=0; i < upipe_avcenc->uref_max; i++) {
                if (unlikely(upipe_avcenc->uref[i])) {
                    upipe_warn_va(upipe, "remaining uref %p freed",
                                  upipe_avcenc->uref[i]);
                    uref_free(upipe_avcenc->uref[i]);
                    upipe_avcenc->uref[i] = NULL;
                }
            }
        }
    }

    /* just closing, that's all */
    if (!codec) {
        upipe_release(upipe);
        return true;
    }

    /* allocate and configure codec context */
    context = avcodec_alloc_context3(codec);
    if (unlikely(!context)) {
        upipe_release(upipe);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }
    context->opaque = upipe;

    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            pix_fmt = *upipe_avcenc->pixfmt->pixfmt;
            if (codec->pix_fmts) {
                pix_fmt = upipe_av_pixfmt_best(upipe_avcenc->pixfmt->pixfmt,
                                               codec->pix_fmts);
            }

            /* aspect ratio */
            struct urational sar = {0, 0};
            if (uref_pic_get_aspect(upipe_avcenc->input_flow, &sar)) {
                context->sample_aspect_ratio.num = sar.num;
                context->sample_aspect_ratio.den = sar.den;
            }

            /* timebase (1/fps) */
            struct urational timebase = {25, 1};
            uref_pic_flow_get_fps(upipe_avcenc->input_flow, &timebase);
            context->time_base.num = timebase.den;
            context->time_base.den = timebase.num;
            context->pix_fmt = pix_fmt;
            context->width = params->width;
            context->height = params->height;

            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            uint8_t channels = 2;
            uint64_t rate = 44100;
            uref_sound_flow_get_channels(upipe_avcenc->input_flow, &channels);
            uref_sound_flow_get_rate(upipe_avcenc->input_flow, &rate);
            context->time_base.num = 1;
            context->time_base.den = 1;//rate;
            context->sample_fmt = upipe_avcenc->sample_fmt;
            context->channels = channels;
            context->channel_layout = av_get_default_channel_layout(channels);
            context->sample_rate = rate;
            break;
        }
        default: {
            av_free(context);
            upipe_err_va(upipe, "Unsupported media type (%d)", codec->type);
            upipe_avcenc->open_failed = true;
            upipe_release(upipe);
            return false;
            break;
        }
    }

    /* user-defined options */
    if (params->options) {
        av_opt_set_dict(context, &params->options);
        /* TODO: display invalid options before freeing dict */
        av_dict_free(&params->options);
    }

    /* open new context */
    if (unlikely(avcodec_open2(context, codec, NULL) < 0)) {
        upipe_warn(upipe, "could not open codec");
        upipe_avcenc->open_failed = true;
        av_free(context);
        upipe_release(upipe);
        return false;
    }

    upipe_avcenc->context = context;
    upipe_avcenc->counter = 0;
    upipe_notice_va(upipe, "codec %s (%s) %d opened (%dx%d)", codec->name,
            codec->long_name, codec->id, context->width, context->height);

    /* allocate uref/avpkt mapping array */
    upipe_avcenc->uref_max = context->delay + 1;
    if (upipe_avcenc->uref_max < 1) {
        upipe_avcenc->uref_max = 1;
    }
    upipe_avcenc->uref = realloc(upipe_avcenc->uref,
                                 upipe_avcenc->uref_max * sizeof(void *));
    memset(upipe_avcenc->uref, 0, upipe_avcenc->uref_max * sizeof(void *));

    upipe_release(upipe);
    return true;
}

/** @internal @This is the open_codec upump callback
 * It calls _open_codec.
 *
 * @param upump description structure of the pump
 */
static void upipe_avcenc_open_codec_cb(struct upump *upump)
{
    assert(upump);
    struct upipe *upipe = upump_get_opaque(upump, struct upipe*);
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    struct upump *upump_av_deal = upipe_avcenc->upump_av_deal;

    /* check udeal */
    if (upump_av_deal) {
        if (unlikely(!upipe_av_deal_grab())) {
            upipe_dbg(upipe, "could not grab resource, return");
            return;
        }
        upipe_avcenc->upump_av_deal = NULL;
    }

    upipe_use(upipe);
    /* real open_codec function */
    upipe_avcenc_open_codec(upipe);

    /* clean dealer */
    upipe_av_deal_yield(upump_av_deal);
    upump_free(upump_av_deal);

    upipe_avcenc_unblock_sink(upipe);
    upipe_avcenc_output_sink(upipe);
    upipe_release(upipe);
}

/** @internal @This wraps open_codec calls (upump/no-upump)
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_avcenc_open_codec_wrap(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);

    /* use udeal/upump callback if available */
    if (upipe_avcenc->upump_mgr) {
        upipe_dbg(upipe, "upump_mgr present, using udeal");

        if (unlikely(upipe_avcenc->upump_av_deal)) {
            upipe_dbg(upipe, "previous upump_av_deal still running, cleaning first");
            upipe_avcenc_abort_av_deal(upipe);
        } else {
            upipe_use(upipe);
        }

        struct upump *upump_av_deal = upipe_av_deal_upump_alloc(upipe_avcenc->upump_mgr,
                                                     upipe_avcenc_open_codec_cb, upipe);
        if (unlikely(!upump_av_deal)) {
            upipe_err(upipe, "can't create dealer");
            upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
            return false;
        }
        upipe_avcenc->upump_av_deal = upump_av_deal;

        /* fire */
        upipe_av_deal_start(upump_av_deal);
        return true;

    } else {
        upipe_dbg(upipe, "no upump_mgr present, direct call to avcenc_open");
        upipe_use(upipe);
        return upipe_avcenc_open_codec(upipe);
    }
}

/** @internal @This finds a codec corresponding to codec definition
 *
 * @param upipe description structure of the pipe
 * @param codec_def codec defintion string
 * @param codec_name codec name string
 * @return false if codec not found
 */
static bool _upipe_avcenc_set_codec(struct upipe *upipe, const char *codec_def,
                                    const char *codec_name)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcenc->open_params;
    AVCodec *codec = NULL;
    int codec_id = 0;

    /* find codec */
    if (codec_name) {
        codec = avcodec_find_encoder_by_name(codec_name);
        if (unlikely(!codec)) {
            upipe_warn_va(upipe, "codec %s not found", codec_name);
            return false;
        }
    } else if (codec_def) {
        codec_id = upipe_av_from_flow_def(codec_def);
        if (unlikely(!codec_id)) {
            upipe_warn_va(upipe, "codec %s not found", codec_def);
            return false;
        }
        codec = avcodec_find_encoder(codec_id);
        if (unlikely(!codec)) {
            upipe_warn_va(upipe, "codec %s (%d) not found", codec_def, codec_id);
            return false;
        }
    }
    params->codec = codec;
    upipe_avcenc->open_failed = false;

    /* call open_codec_wrap at once to close codec if codec == NULL.
     * else openc_codec_wrap shall be called upon receiving next frame.
     */
    if (unlikely(!codec)) {
        upipe_dbg(upipe, "close current codec");
        return upipe_avcenc_open_codec_wrap(upipe);
    }

    if (codec_name)
        upipe_dbg_va(upipe, "codec %s set", codec_name);
    else
        upipe_dbg_va(upipe, "codec %s (%d) set", codec_def, codec_id);
    return true;
}

/** @internal @This encodes incoming frames.
 *
 * @param upipe description structure of the pipe
 * @param frame frame
 * @param upump upump structure
 * @return true when a packet has been output
 */
static bool upipe_avcenc_encode_frame(struct upipe *upipe,
                                      struct AVFrame *frame,
                                      struct upump *upump)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    const struct upipe_av_plane *plane = upipe_avcenc->pixfmt->planes;
    AVCodecContext *context = upipe_avcenc->context;
    const AVCodec *codec = context->codec;
    AVPacket avpkt;
    int i, gotframe, ret, size;
    struct ubuf *ubuf_block;
    uint8_t *buf;
    int64_t priv = 0;

    if (unlikely(frame == NULL)) {
        /* uref == NULL, flushing encoder */
        upipe_dbg(upipe, "received null frame");
        if (unlikely(!context || !(codec->capabilities & CODEC_CAP_DELAY))) {
                return false;
        }
    }

    /* encode frame */
    av_init_packet(&avpkt);
    avpkt.data = NULL;
    avpkt.size = 0;
    gotframe = 0;
    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            ret = avcodec_encode_video2(context, &avpkt, frame, &gotframe);
            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            ret = avcodec_encode_audio2(context, &avpkt, frame, &gotframe);
            break;
        }
        default: /* should never be there */
            return false;
    }

    if (ret < 0) {
        upipe_warn(upipe, "error while encoding frame");
        return false;
    }

    /* output encoded frame if available */
    if (!(gotframe && avpkt.data)) {
        return false;
    }

    /* flow definition */
    if (unlikely(!upipe_avcenc->output_flow)) {
        const char *codec_def = upipe_av_to_flow_def(codec->id);
        char def[strlen(PREFIX_FLOW) + strlen(codec_def) + 1];
        snprintf(def, sizeof(def), PREFIX_FLOW "%s", codec_def);

        struct uref *outflow = uref_dup(upipe_avcenc->input_flow);
        uref_flow_set_def(outflow, def);
        uref_block_flow_delete_octetrate(outflow);
        uref_block_flow_delete_cpb_buffer(outflow);
        if (context->bit_rate) {
            uref_block_flow_set_octetrate(outflow, context->bit_rate / 8);
            if (context->rc_buffer_size)
                uref_block_flow_set_cpb_buffer(outflow,
                                               context->rc_buffer_size / 8);
            else if (codec->type == AVMEDIA_TYPE_AUDIO &&
                     strcmp(codec->name, "mp2") && strcmp(codec->name, "mp3")) {
                uref_block_flow_set_cpb_buffer(outflow, AUDIO_BS);
                upipe_avcenc->audio_bs_duration = AUDIO_BS * UCLOCK_FREQ /
                                                  (context->bit_rate / 8);
                upipe_avcenc->audio_bs_leakage = UCLOCK_FREQ *
                    context->frame_size / context->sample_rate;
                upipe_avcenc->audio_bs_delay = upipe_avcenc->audio_bs_duration;
            }
        }
        upipe_avcenc_store_flow_def(upipe, outflow);
    }

    size = -1;
    ubuf_block = ubuf_block_alloc(upipe_avcenc->ubuf_mgr, avpkt.size);
    ubuf_block_write(ubuf_block, 0, &size, &buf);
    memcpy(buf, avpkt.data, size);
    ubuf_block_unmap(ubuf_block, 0);
    free(avpkt.data);

    /* find uref corresponding to avpkt */
    struct uref *uref = NULL;
    for (i=0; i < upipe_avcenc->uref_max; i++) {
        if (upipe_avcenc->uref[i]) {
            priv = 0;
            if (uref_avcenc_get_pts(upipe_avcenc->uref[i], &priv)
                                           && priv == avpkt.pts) {
                uref = upipe_avcenc->uref[i];
                upipe_avcenc->uref[i] = NULL;
                break;
            }
        }
    }
    if (unlikely(!uref)) {
        upipe_warn_va(upipe, "could not find pts %"PRId64" in current urefs",
                                                                  avpkt.pts);
        uref = uref_alloc(upipe_avcenc->uref_mgr);
    }
    uref_avcenc_delete_pts(uref);

    /* set dts */
    uint64_t dts_pts_delay = (uint64_t)(avpkt.pts - avpkt.dts) * UCLOCK_FREQ
                              * context->time_base.num
                              / context->time_base.den;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);

    /* rebase to dts as we're in encoded domain now */
    uref_clock_rebase_dts_sys(uref);
    uref_clock_rebase_dts_prog(uref);
    uref_clock_rebase_dts_orig(uref);

    /* vbv delay */
    if (context->vbv_delay) {
        uref_clock_set_cr_dts_delay(uref, context->vbv_delay);
    } else if (codec->type == AVMEDIA_TYPE_AUDIO &&
               strcmp(codec->name, "mp2") && strcmp(codec->name, "mp3")) {
        upipe_avcenc->audio_bs_delay += upipe_avcenc->audio_bs_leakage;
        upipe_avcenc->audio_bs_delay -= size * UCLOCK_FREQ /
                                 (context->bit_rate / 8);
        if (upipe_avcenc->audio_bs_delay < 0) {
            upipe_warn_va(upipe, "audio BS underflow %"PRId64,
                          -upipe_avcenc->audio_bs_delay);
            upipe_avcenc->audio_bs_delay = 0;
        } else if (upipe_avcenc->audio_bs_delay >
                   upipe_avcenc->audio_bs_duration)
            upipe_avcenc->audio_bs_delay = upipe_avcenc->audio_bs_duration;
        uref_clock_set_cr_dts_delay(uref, upipe_avcenc->audio_bs_delay);
    } else {
        uref_clock_delete_cr_dts_delay(uref);
    }

    /* unmap input and clean */
    switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            for (i=0; i < 4 && plane[i].chroma; i++)
                uref_pic_plane_unmap(uref, plane[i].chroma, 0, 0, -1, -1);
            break;
        }
        case AVMEDIA_TYPE_AUDIO: {
            uref_block_unmap(uref, 0);
            break;
        }
        default: /* should never be there */
            return false;
    }
    if (uref->ubuf) {
        ubuf_free(uref_detach_ubuf(uref));
    }

    uref_attach_ubuf(uref, ubuf_block);

    upipe_avcenc_output(upipe, uref, upump);
    return true;
}

/** @internal @This handles incoming frames.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 * @return false if the frame couldn't be encoded yet
 */
static bool upipe_avcenc_input_frame(struct upipe *upipe,
                                     struct uref *uref, struct upump *upump)
{
    const struct upipe_av_plane *plane = NULL;
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    struct upipe_avcodec_open_params *params = &upipe_avcenc->open_params;
    AVFrame *frame;
    size_t stride, width = 0, height = 0;
    int i;
    uint64_t pts = 0;

    /* check codec first */
    if (unlikely(!params->codec && !upipe_avcenc->context)) {
        uref_free(uref);
        upipe_warn(upipe, "received frame but encoder is not set");
        return true;
    }

    /* detect input format */
    if (uref_pic_size(uref, &width, &height, NULL)) {
        if (unlikely(!upipe_avcenc->pixfmt)) {
            upipe_avcenc->pixfmt = upipe_av_pixfmt_from_ubuf(uref->ubuf);
            if (unlikely(!upipe_avcenc->pixfmt)) {
                upipe_warn(upipe, "unrecognized input format");
                uref_free(uref);
                return true;
            }
        }
    }

    /* open context */
    if (unlikely(!upipe_avcenc->context)) {
        if (upipe_avcenc->open_failed) {
            uref_free(uref);
            return true;
        }
        upipe_dbg_va(upipe, "received frame (%dx%d), opening codec",
                                                        width, height);

        params->width  = width;
        params->height = height;
        upipe_avcenc_open_codec_wrap(upipe);
        if (upipe_avcenc->open_failed) {
            uref_free(uref);
            return true;
        }

        /* if open_codec still pending, save uref and return */
        if (!upipe_avcenc->context) {
            upipe_avcenc_block_sink(upipe, upump);
            upipe_avcenc_hold_sink(upipe, uref);
            return false;
        }
    }
    AVCodecContext *context = upipe_avcenc->context;

    /* map input */
    frame  = upipe_avcenc->frame;
    switch (context->codec->type) {
        case AVMEDIA_TYPE_VIDEO: {
            plane = upipe_avcenc->pixfmt->planes;
            for (i=0; i < 4 && plane[i].chroma; i++) {
                uref_pic_plane_read(uref, plane[i].chroma,
                        0, 0, -1, -1, (const uint8_t **)&frame->data[i]);
                uref_pic_plane_size(uref, plane[i].chroma, &stride,
                        NULL, NULL, NULL);
                frame->linesize[i] = stride;
            }
            break;
        }

        case AVMEDIA_TYPE_AUDIO: {
            const uint8_t *buf = NULL;
            int size = av_samples_get_buffer_size(NULL, context->channels,
                        context->frame_size, context->sample_fmt, 0);
            
            size_t blocksize = 0, remaining = 0;
            uref_block_size(uref, &blocksize);

            /* audio packets must be of a codec-specific size ... */
            int64_t junk;
            if (!uref_avcenc_get_pts(uref, &junk)) {
                upipe_avcenc_append_sound_stream(upipe, uref);

                while (upipe_avcenc->next_uref &&
                       uref_block_size(upipe_avcenc->next_uref, &remaining) &&
                       (remaining >= size)) {
                    uref = upipe_avcenc_extract_sound_stream(upipe, size,
                        context->channels,
                        av_get_bytes_per_sample(context->sample_fmt),
                        context->sample_rate);
                    uref_avcenc_set_pts(uref, 0);
                    upipe_avcenc_input_frame(upipe, uref, upump);
                }
                return true;
            }

            uref_sound_flow_delete_samples(uref);

            frame->nb_samples = context->frame_size;
            frame->format = context->sample_fmt;
            frame->channel_layout = context->channel_layout;

            /* merge segmented urefs */
            uref_block_size(uref, &blocksize);
            uref_block_merge(uref, upipe_avcenc->ubuf_mgr, 0, blocksize);
            uref_block_read(uref, 0, &size, &buf);

            avcodec_fill_audio_frame(frame, context->channels,
                                     context->sample_fmt, buf, size, 0);
            break;
        }
        default: {
            uref_free(uref);
            return false;
        }
    }

    /* set pts (needed for uref/avpkt mapping) */
    frame->pts = upipe_avcenc->avcpts++;
    uref_avcenc_set_pts(uref, frame->pts);

    /* aspect ratio */
    struct urational sar = {0, 0};
    if (uref_pic_get_aspect(uref, &sar)) {
        context->sample_aspect_ratio.num = sar.num;
        context->sample_aspect_ratio.den = sar.den;
    }

    /* store uref in mapping array */
    for (i=0; i < upipe_avcenc->uref_max
              && upipe_avcenc->uref[i]; i++);
    if (unlikely(i == upipe_avcenc->uref_max)) {
        upipe_dbg_va(upipe, "mapping array too small (%d), resizing",
                     upipe_avcenc->uref_max);
        upipe_avcenc->uref = realloc(upipe_avcenc->uref,
                                     2*upipe_avcenc->uref_max*sizeof(void*));
        memset(upipe_avcenc->uref+upipe_avcenc->uref_max, 0,
               upipe_avcenc->uref_max * sizeof(void*));
        upipe_avcenc->uref_max *= 2;
    }
    upipe_verbose_va(upipe, "uref %p (%"PRIu64") stored at index %d",
                 uref, pts, i);
    upipe_avcenc->uref[i] = uref;

    upipe_avcenc_encode_frame(upipe, frame, upump);
    return true;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_avcenc_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (upipe_avcenc->uref_mgr == NULL) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_avcenc->uref_mgr == NULL)) {
            uref_free(uref);
            return;
        }
    }

    assert(uref);
    if (unlikely(!uref->ubuf)) {
        upipe_avcenc_output(upipe, uref, upump);
        return;
    }

    /* check ubuf manager */
    if (unlikely(!upipe_avcenc->ubuf_mgr)) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_avcenc->output_flow);
        if (unlikely(!upipe_avcenc->ubuf_mgr)) {
            upipe_warn(upipe, "ubuf_mgr not set !");
            uref_free(uref);
            return;
        }
    }

    upipe_avcenc_input_frame(upipe, uref, upump);
}

/** @internal @This returns the current codec definition string
 */
static bool _upipe_avcenc_get_codec(struct upipe *upipe, const char **codec_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    assert(codec_p);
    if (upipe_avcenc->context && upipe_avcenc->context->codec) {
        *codec_p = upipe_av_to_flow_def(upipe_avcenc->context->codec->id);
    } else {
        *codec_p = NULL;
        return false;
    }
    return true;
}

/** @internal @This sets the content of an avcodec option. It only take effect
 * after the next call to @ref upipe_avcenc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool _upipe_avcenc_set_option(struct upipe *upipe, const char *option,
                                     const char *content)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVDictionary **dict = &(upipe_avcenc->open_params.options);
    if (upipe_avcenc->context && avcodec_is_open(upipe_avcenc->context)) {
        return false;
    }
    assert(option != NULL);
    int error = av_dict_set(dict, option, content, 0);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)",
                     option, content, buf);
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
static bool upipe_avcenc_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    switch (command) {
        /* generic linear stuff */
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_avcenc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_avcenc_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_avcenc_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_avcenc_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_avcenc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avcenc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_avcenc_set_output(upipe, output);
        }
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_avcenc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_avcenc_set_upump_av_deal(upipe, NULL);
            upipe_avcenc_abort_av_deal(upipe);
            return upipe_avcenc_set_upump_mgr(upipe, upump_mgr);
        }

        case UPIPE_AVCENC_GET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCENC_SIGNATURE);
            const char **url_p = va_arg(args, const char **);
            return _upipe_avcenc_get_codec(upipe, url_p);
        }
        case UPIPE_AVCENC_SET_CODEC: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCENC_SIGNATURE);
            const char *codec = va_arg(args, const char *);
            return _upipe_avcenc_set_codec(upipe, codec, NULL);
        }
        case UPIPE_AVCENC_SET_CODEC_BY_NAME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCENC_SIGNATURE);
            const char *codec = va_arg(args, const char *);
            return _upipe_avcenc_set_codec(upipe, NULL, codec);
        }
        case UPIPE_AVCENC_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVCENC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avcenc_set_option(upipe, option, content);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_free(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (upipe_avcenc->context) {
        _upipe_avcenc_set_codec(upipe, NULL, NULL);
        return; /* _set_codec() calls _use()/_release() */
    }

    uref_free(upipe_avcenc->input_flow);
    if (upipe_avcenc->frame) {
        av_free(upipe_avcenc->frame);
    }

    free(upipe_avcenc->uref);

    upipe_avcenc_abort_av_deal(upipe);
    upipe_avcenc_clean_sink(upipe);
    upipe_avcenc_clean_ubuf_mgr(upipe);
    upipe_avcenc_clean_uref_mgr(upipe);
    upipe_avcenc_clean_upump_av_deal(upipe);
    upipe_avcenc_clean_upump_mgr(upipe);
    upipe_avcenc_clean_sound_stream(upipe);

    upipe_throw_dead(upipe);
    upipe_avcenc_clean_output(upipe);
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
    struct uref *flow_def;
    const char *def = NULL;
    struct upipe *upipe = upipe_avcenc_alloc_flow(mgr, uprobe, signature,
                                                  args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uref_flow_get_def(flow_def, &def);

    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    upipe_avcenc_init_uref_mgr(upipe);
    upipe_avcenc_init_ubuf_mgr(upipe);
    upipe_avcenc_init_upump_mgr(upipe);
    upipe_avcenc_init_upump_av_deal(upipe);
    upipe_avcenc_init_output(upipe);
    upipe_avcenc_init_sink(upipe);
    upipe_avcenc_init_sound_stream(upipe);

    upipe_avcenc->input_flow = flow_def;
    upipe_avcenc->context = NULL;
    upipe_avcenc->open_failed = false;
    upipe_avcenc->upump_av_deal = NULL;
    upipe_avcenc->pixfmt = NULL;
    upipe_avcenc->frame = avcodec_alloc_frame();
    upipe_avcenc->uref = NULL;
    upipe_avcenc->uref_max = 1;
    upipe_avcenc->avcpts = 1;
    upipe_avcenc->sample_fmt = upipe_av_samplefmt_from_flow_def(def);
    upipe_avcenc->audio_bs_duration = 0;
    upipe_avcenc->audio_bs_leakage = 0;
    upipe_avcenc->audio_bs_delay = 0;

    memset(&upipe_avcenc->open_params, 0, sizeof(struct upipe_avcodec_open_params));

    upipe_throw_ready(upipe);

    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avcenc_mgr = {
    .signature = UPIPE_AVCENC_SIGNATURE,

    .upipe_alloc = upipe_avcenc_alloc,
    .upipe_input = upipe_avcenc_input,
    .upipe_control = upipe_avcenc_control,
    .upipe_free = upipe_avcenc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for avcenc pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcenc_mgr_alloc(void)
{
    return &upipe_avcenc_mgr;
}
