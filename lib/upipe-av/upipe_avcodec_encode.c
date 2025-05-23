/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools S.A.S.
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

#include "upipe/uclock.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_sound.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/uref_dump.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_flow_format.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe/upipe_helper_flow_def_check.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_input.h"
#include "upipe-av/upipe_avcodec_encode.h"
#include "upipe-av/ubuf_av.h"
#include "upipe-framers/uref_h264.h"
#include "upipe-framers/uref_h265.h"
#include "upipe-framers/uref_mpgv.h"
#include "upipe-framers/uref_mpga_flow.h"
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
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
#include <bitstream/mpeg/h264.h>
#include <bitstream/itu/h265.h>
#include <bitstream/mpeg/mp2v.h>
#include <bitstream/dvb/sub.h>

#include "upipe-av/upipe_av_pixfmt.h"
#include "upipe-av/upipe_av_samplefmt.h"
#include "upipe_av_internal.h"

#define PREFIX_FLOW "block."

UREF_ATTR_INT(avcenc, priv, "x.avcenc_priv", avcenc private pts)

/** start offset of avcodec PTS */
#define AVCPTS_INIT 1
/** T-STD TB octet rate for DVB subtitles with display definition segment */
#define TB_RATE_DVBSUB_DISP 50000
/** DVB subtitles buffer size with display definition segment
 * (ETSI EN 300 743 5.) */
#define BS_DVBSUB_DISP 102400

/** @hidden */
static int upipe_avcenc_check_ubuf_mgr(struct upipe *upipe,
                                       struct uref *flow_format);
/** @hidden */
static int upipe_avcenc_check_flow_format(struct upipe *upipe,
                                          struct uref *flow_format);
/** @hidden */
static int upipe_avcenc_encode_frame(struct upipe *upipe,
                                     struct AVFrame *frame,
                                     struct upump **upump_p);
/** @hidden */
static void upipe_avcenc_encode_audio(struct upipe *upipe,
                                      struct upump **upump_p);
/** @hidden */
static bool upipe_avcenc_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);
/** @hidden */
static int upipe_avcenc_set_option(struct upipe *upipe,
                                   const char *option, const char *content);

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
    /** requested flow */
    struct uref *flow_def_requested;
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
    /** flow format request */
    struct urequest flow_format_request;

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
    /** last PTS */
    uint64_t last_pts;
    /** last DTS */
    uint64_t last_dts;
    /** last DTS (system time) */
    uint64_t last_dts_sys;
    /** drift rate */
    struct urational drift_rate;
    /** last input PTS */
    uint64_t input_pts;
    /** last input PTS (system time) */
    uint64_t input_pts_sys;
    /** latency in the input flow */
    uint64_t input_latency;

    /** frame counter */
    uint64_t counter;
    /** chroma map */
    const char *chroma_map[UPIPE_AV_MAX_PLANES];

    /** true if the existing slice types must be enforced */
    bool slice_type_enforce;
    /** uref serving as a dictionary for options */
    struct uref *options;

    /** avcodec context */
    AVCodecContext *context;
    /** avcodec frame */
    AVFrame *frame;
    /** avcodec packet */
    AVPacket *avpkt;
    /** true if the context will be closed */
    bool close;
    /** true if the context will be reinitialized */
    bool reinit;
    /** true if the pipe need to be released after output_input */
    bool release_needed;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avcenc, upipe, UPIPE_AVCENC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_avcenc, urefcount, upipe_avcenc_close)
UPIPE_HELPER_FLOW(upipe_avcenc, "block.")
UPIPE_HELPER_OUTPUT(upipe_avcenc, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_avcenc, urefs, nb_urefs, max_urefs, blockers, upipe_avcenc_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_avcenc, flow_format_request,
                         upipe_avcenc_check_flow_format,
                         upipe_avcenc_register_output_request,
                         upipe_avcenc_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_avcenc, flow_def_input, flow_def_attr)
UPIPE_HELPER_FLOW_DEF_CHECK(upipe_avcenc, flow_def_check)
UPIPE_HELPER_UBUF_MGR(upipe_avcenc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_avcenc_check_ubuf_mgr,
                      upipe_avcenc_register_output_request,
                      upipe_avcenc_unregister_output_request)
UPIPE_HELPER_UPUMP_MGR(upipe_avcenc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avcenc, upump_av_deal, upump_mgr)

/** @hidden */
static void upipe_avcenc_free(struct upipe *upipe);

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

/** @internal @This closes and reinitializes the avcodec context.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_avcenc_do_reinit(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;

    const struct AVCodec *codec = context->codec;
    int flags = context->flags;
    int width = context->width;
    int height = context->height;
    enum AVPixelFormat pix_fmt = context->pix_fmt;
    AVRational time_base = context->time_base;
    AVRational framerate = context->framerate;
    AVRational sample_aspect_ratio = context->sample_aspect_ratio;
    enum AVFieldOrder field_order = context->field_order;
    enum AVColorRange color_range = context->color_range;
    enum AVColorPrimaries color_primaries = context->color_primaries;
    enum AVColorTransferCharacteristic color_trc = context->color_trc;
    enum AVColorSpace colorspace = context->colorspace;

    avcodec_free_context(&context);
    context = avcodec_alloc_context3(codec);
    if (context == NULL) {
        upipe_err(upipe, "cannot allocate codec context");
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    context->opaque = upipe;
    context->flags = flags;
    context->width = width;
    context->height = height;
    context->pix_fmt = pix_fmt;
    context->time_base = time_base;
    context->framerate = framerate;
    context->sample_aspect_ratio = sample_aspect_ratio;
    context->field_order = field_order;
    context->color_range = color_range;
    context->color_primaries = color_primaries;
    context->color_trc = color_trc;
    context->colorspace = colorspace;

    if (upipe_avcenc->options != NULL &&
        upipe_avcenc->options->udict != NULL) {
        const char *key = NULL;
        enum udict_type type = UDICT_TYPE_END;
        while (ubase_check(udict_iterate(upipe_avcenc->options->udict, &key,
                                         &type)) && type != UDICT_TYPE_END) {
            const char *value;
            if (key == NULL ||
                !ubase_check(udict_get_string(upipe_avcenc->options->udict,
                                              &value, type, key)))
                continue;
            int err = av_opt_set(context, key, value, AV_OPT_SEARCH_CHILDREN);
            if (err < 0)
                upipe_warn_va(upipe, "invalid option %s=%s (%s)", key, value,
                              av_err2str(err));
        }
    }

    upipe_avcenc->context = context;
    return UBASE_ERR_NONE;
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

    /* reinit context */
    if (upipe_avcenc->reinit)
        return ubase_check(upipe_avcenc_do_reinit(upipe));

    /* open new context */
    int err;
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

    bool was_buffered = !upipe_avcenc_check_input(upipe);
    if (ret)
        upipe_avcenc_output_input(upipe);
    else
        upipe_avcenc_flush_input(upipe);
    upipe_avcenc_unblock_input(upipe);
    if (was_buffered && upipe_avcenc_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_avcenc_input. */
        if (upipe_avcenc->release_needed) {
            upipe_release(upipe);
            upipe_avcenc->release_needed = false;
        }
    }
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
                upipe_avcenc_cb_av_deal, upipe, upipe->refcount);
    if (unlikely(!upump_av_deal)) {
        upipe_err(upipe, "can't create dealer");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    upipe_avcenc->upump_av_deal = upump_av_deal;
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
    upipe_avcenc->reinit = false;
    upipe_avcenc_start_av_deal(upipe);
}

/** @internal @This is called to trigger context reinitialization.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_reinit(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    upipe_avcenc->close = false;
    upipe_avcenc->reinit = true;
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

        if (context->codec->capabilities & AV_CODEC_CAP_DELAY) {
            /* Feed avcodec with NULL frame to output the remaining packets. */
            upipe_avcenc_encode_frame(upipe, NULL, NULL);
        }
    }
    upipe_avcenc->close = true;
    upipe_avcenc_start_av_deal(upipe);
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_build_flow_def(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    assert(upipe_avcenc->flow_def_requested != NULL);

    struct uref *flow_def = uref_dup(upipe_avcenc->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (context->bit_rate) {
        uint64_t octetrate = context->bit_rate / 8;

        if (!strcmp(context->codec->name, "libopus"))
            octetrate += context->sample_rate / context->frame_size;
        uref_block_flow_set_octetrate(flow_def, octetrate);
        if (context->rc_buffer_size)
            uref_block_flow_set_buffer_size(flow_def,
                                            context->rc_buffer_size / 8);
    }

    if (context->codec->type == AVMEDIA_TYPE_AUDIO && context->frame_size > 0)
        uref_sound_flow_set_samples(flow_def, context->frame_size);

    if (!strcmp(context->codec->name, "libfdk_aac")) {
        /* That's actually how it's defined. */
        UBASE_FATAL(upipe,
                    uref_mpga_flow_set_aot(flow_def, context->profile + 1))
    }

    /* global headers (extradata) */
    if (context->extradata_size > 0 &&
        (context->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
        UBASE_FATAL(upipe,
                uref_flow_set_headers(flow_def, context->extradata,
                                      context->extradata_size))
    }

    if (context->codec->id == AV_CODEC_ID_DVB_SUBTITLE) {
        int type = 0x10;
        if (ubase_check(uref_flow_get_hearing_impaired(flow_def, 0)))
            type += 0x10;
        UBASE_FATAL(upipe, uref_ts_flow_set_sub_type(flow_def, type, 0))
        UBASE_FATAL(upipe, uref_ts_flow_set_sub_composition(flow_def, 1, 0))
        UBASE_FATAL(upipe, uref_ts_flow_set_sub_ancillary(flow_def, 1, 0))
    }

    /* find latency */
    if (upipe_avcenc->input_pts != UINT64_MAX) {
        uint64_t latency = upipe_avcenc->input_pts - upipe_avcenc->last_pts;
        upipe_notice_va(upipe, "latency: %" PRIu64 " ms",
                        1000 * latency / UCLOCK_FREQ);
        uref_clock_set_latency(flow_def, upipe_avcenc->input_latency + latency);
    }

    upipe_avcenc_store_flow_def(upipe, flow_def);
}

/** @internal @This outputs av packet.
 *
 * @param upipe description structure of the pipe
 * @param avpkt av packet
 * @param upump_p reference to upump structure
 */
static void upipe_avcenc_output_pkt(struct upipe *upipe,
                                    struct AVPacket *avpkt,
                                    struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    const AVCodec *codec = context->codec;

    int extra_size = 0;
    if (!strcmp(codec->name, "mpeg2_qsv") &&
        (avpkt->flags & AV_PKT_FLAG_KEY))
        extra_size = context->extradata_size;

    struct ubuf *ubuf = ubuf_block_alloc(upipe_avcenc->ubuf_mgr,
                                         avpkt->size + extra_size);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    int size = -1;
    uint8_t *buf;
    if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &size, &buf)))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    if (extra_size > 0)
        memcpy(buf, context->extradata, context->extradata_size);
    memcpy(buf + extra_size, avpkt->data, avpkt->size);
    ubuf_block_unmap(ubuf, 0);

    int64_t pkt_pts = avpkt->pts, pkt_dts = avpkt->dts;
    bool keyframe = avpkt->flags & AV_PKT_FLAG_KEY;

    /* find uref corresponding to avpkt */
    upipe_verbose_va(upipe, "output pts %"PRId64, pkt_pts);
    struct uchain *uchain;
    struct uchain *uchain_tmp;
    struct uref *uref = NULL;
    ulist_delete_foreach (&upipe_avcenc->urefs_in_use, uchain, uchain_tmp) {
        struct uref *uref_chain = uref_from_uchain(uchain);
        int64_t priv = 0;
        if (ubase_check(uref_avcenc_get_priv(uref_chain, &priv)) && priv == pkt_pts) {
            uref = uref_chain;
            ulist_delete(uchain);
            break;
        }
    }
    if (unlikely(uref == NULL)) {
        upipe_warn_va(upipe, "could not find pts %"PRId64" in urefs in use",
                      pkt_pts);
        ubuf_free(ubuf);
        return;
    }

    /* unmap input */
    if (codec->type == AVMEDIA_TYPE_VIDEO)
        for (int i = 0; i < UPIPE_AV_MAX_PLANES &&
             upipe_avcenc->chroma_map[i] != NULL; i++)
            uref_pic_plane_unmap(uref, upipe_avcenc->chroma_map[i],
                                 0, 0, -1, -1);

    uref_attach_ubuf(uref, ubuf);
    uref_avcenc_delete_priv(uref);

    /* set dts */
    uint64_t dts_pts_delay = (uint64_t)(pkt_pts - pkt_dts) * UCLOCK_FREQ
                              * context->time_base.num
                              / context->time_base.den;
    uref_clock_set_dts_pts_delay(uref, dts_pts_delay);
    uref_clock_delete_cr_dts_delay(uref);

    /* rebase to dts as we're in encoded domain now */
    uint64_t dts = UINT64_MAX;
    if ((!ubase_check(uref_clock_get_dts_prog(uref, &dts)) ||
         dts < upipe_avcenc->last_dts) &&
        upipe_avcenc->last_dts != UINT64_MAX) {
        upipe_warn_va(upipe, "DTS prog in the past, resetting (%"PRIu64" ms)",
                      (upipe_avcenc->last_dts - dts) * 1000 / UCLOCK_FREQ);
        dts = upipe_avcenc->last_dts + 1;
        uref_clock_set_dts_prog(uref, dts);
    } else
        uref_clock_rebase_dts_prog(uref);

    uint64_t dts_sys = UINT64_MAX;
    if (dts != UINT64_MAX &&
        upipe_avcenc->input_pts != UINT64_MAX &&
        upipe_avcenc->input_pts_sys != UINT64_MAX) {
        dts_sys = (int64_t)upipe_avcenc->input_pts_sys +
            ((int64_t)dts - (int64_t)upipe_avcenc->input_pts) *
            (int64_t)upipe_avcenc->drift_rate.num /
            (int64_t)upipe_avcenc->drift_rate.den;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else if (!ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) ||
        (upipe_avcenc->last_dts_sys != UINT64_MAX &&
               dts_sys < upipe_avcenc->last_dts_sys)) {
        upipe_warn_va(upipe,
                      "DTS sys in the past, resetting (%"PRIu64" ms)",
                      (upipe_avcenc->last_dts_sys - dts_sys) * 1000 /
                      UCLOCK_FREQ);
        dts_sys = upipe_avcenc->last_dts_sys + 1;
        uref_clock_set_dts_sys(uref, dts_sys);
    } else
        uref_clock_rebase_dts_sys(uref);

    if (codec->type == AVMEDIA_TYPE_AUDIO) {
        if (context->sample_rate)
            uref_clock_set_duration(uref, context->frame_size * UCLOCK_FREQ /
                                    context->sample_rate);
    }

    uref_clock_rebase_dts_orig(uref);
    uref_clock_set_rate(uref, upipe_avcenc->drift_rate);
    uref_clock_get_pts_prog(uref, &upipe_avcenc->last_pts);

    upipe_avcenc->last_dts = dts;
    upipe_avcenc->last_dts_sys = dts_sys;

    if (codec->type == AVMEDIA_TYPE_VIDEO && keyframe)
        uref_flow_set_random(uref);

    if (upipe_avcenc->flow_def == NULL)
        upipe_avcenc_build_flow_def(upipe);

    upipe_avcenc_output(upipe, uref, upump_p);
}

/** @internal @This encodes av frames.
 *
 * @param upipe description structure of the pipe
 * @param frame frame
 * @param upump_p reference to upump structure
 * @return an error code
 */
static int upipe_avcenc_encode_frame(struct upipe *upipe,
                                     struct AVFrame *frame,
                                     struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;

    if (unlikely(frame == NULL))
        upipe_dbg(upipe, "received null frame");

    /* encode frame */
    int err = avcodec_send_frame(context, frame);
    av_frame_unref(frame);
    if (unlikely(err < 0)) {
        upipe_err_va(upipe, "avcodec_send_frame: %s", av_err2str(err));
        return UBASE_ERR_EXTERNAL;
    }

    while (1) {
        err = avcodec_receive_packet(context, upipe_avcenc->avpkt);
        if (unlikely(err < 0)) {
            if (err != AVERROR(EAGAIN) &&
                err != AVERROR_EOF)
                upipe_err_va(upipe, "avcodec_receive_packet: %s",
                             av_err2str(err));
            break;
        }
        upipe_avcenc_output_pkt(upipe, upipe_avcenc->avpkt, upump_p);
    }
    av_packet_unref(upipe_avcenc->avpkt);

    return UBASE_ERR_NONE;
}

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

/** @internal @This encodes subtitles.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_avcenc_encode_subtitle(struct upipe *upipe,
                                         struct uref *uref,
                                         struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;

    const uint8_t *data;
    size_t stride;
    if (unlikely(
            !ubase_check(uref_pic_plane_size(
                    uref, "b8g8r8a8", &stride, NULL, NULL, NULL)) ||
            !ubase_check(uref_pic_plane_read(
                    uref, "b8g8r8a8", 0, 0, -1, -1, &data)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    int x_max = 0, x_min = context->width;
    int y_max = 0, y_min = context->height;

    for (int y = 0; y < context->height; y++)
        for (int x = 0; x < context->width; x++)
            if (data[y * stride + x * 4 + 3]) {
                y_min = MIN(y_min, y);
                y_max = MAX(y_max, y);
                x_min = MIN(x_min, x);
                x_max = MAX(x_max, x);
            }

    int x = x_min;
    int y = y_min;
    int width;
    int height;
    uint32_t palette[256];
    uint8_t plane[context->width * context->height];
    int colors = 0;
    int num_rects = 1;

    if (y == context->height) {
        /* blank subtitle */
        num_rects = 0;
        x = 0;
        y = 0;
        width = 0;
        height = 0;
    } else {
        height = y_max - y_min + 1;
        width = x_max - x_min + 1;
        for (int h = 0; h < height; h++)
            for (int w = 0; w < width; w++) {
                const uint8_t *p = data + (y + h) * stride + (x + w) * 4;
                uint32_t c = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
                int i;
                for (i = 0; i < colors; i++)
                    if (palette[i] == c)
                        break;
                if (i == colors) {
                    if (colors < UBASE_ARRAY_SIZE(palette))
                        palette[colors++] = c;
                    else
                        i = 0;
                }
                plane[h * width + w] = i;
            }
    }

    uref_pic_plane_unmap(uref, "b8g8r8a8", 0, 0, -1, -1);

    struct AVSubtitleRect rect = {
        .type = SUBTITLE_BITMAP,
        .x = x,
        .y = y,
        .w = width,
        .h = height,
        .nb_colors = colors,
        .data = { plane, (uint8_t *) palette },
        .linesize = { width },
    };
    AVSubtitleRect *rects = &rect;
    AVSubtitle subtitle = {
        .num_rects = num_rects,
        .rects = num_rects > 0 ? &rects : NULL,
    };

    /* set pts (needed for uref/avpkt mapping) */
    upipe_verbose_va(upipe, "input pts %"PRId64, upipe_avcenc->avcpts);
    int64_t pts = upipe_avcenc->avcpts++;
    upipe_avcenc->avpkt->pts = upipe_avcenc->avpkt->dts = pts;
    if (unlikely(!ubase_check(uref_avcenc_set_priv(uref, pts)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* store uref in mapping list */
    ulist_add(&upipe_avcenc->urefs_in_use, uref_to_uchain(uref));

    uint8_t buf[1024 * 1024];
    buf[0] = DVBSUB_DATA_IDENTIFIER;
    buf[1] = 0x00;
    int err = avcodec_encode_subtitle(context, buf + 2, sizeof buf - 3,
                                      &subtitle);
    if (err < 0) {
        upipe_err_va(upipe, "avcodec_encode_subtitle: %s", av_err2str(err));
        uref_free(uref);
        return;
    }

    buf[2 + err] = 0xff;
    upipe_avcenc->avpkt->data = buf;
    upipe_avcenc->avpkt->size = 3 + err;
    upipe_avcenc_output_pkt(upipe, upipe_avcenc->avpkt, upump_p);
    av_packet_unref(upipe_avcenc->avpkt);
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

    if (!ubase_check(ubuf_av_get_avframe(uref->ubuf, frame))) {
        for (int i = 0; i < UPIPE_AV_MAX_PLANES &&
             upipe_avcenc->chroma_map[i] != NULL; i++) {
            const uint8_t *data;
            size_t stride;
            if (unlikely(!ubase_check(uref_pic_plane_read(
                            uref, upipe_avcenc->chroma_map[i],
                            0, 0, -1, -1, &data)) ||
                    !ubase_check(uref_pic_plane_size(
                            uref, upipe_avcenc->chroma_map[i],
                            &stride, NULL, NULL, NULL)))) {
                upipe_warn(upipe, "invalid buffer received");
                uref_free(uref);
                return;
            }
            frame->data[i] = (uint8_t *)data;
            frame->linesize[i] = stride;
        }

        /* set frame properties */
        frame->format = context->pix_fmt;
        frame->width = hsize;
        frame->height = vsize;

        if (!ubase_check(upipe_av_set_frame_properties(
                    upipe, frame, upipe_avcenc->flow_def_attr, uref))) {
            uref_free(uref);
            return;
        }
    }

    /* set picture type */
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    if (upipe_avcenc->slice_type_enforce) {
        uint8_t type;
        if (ubase_check(uref_h264_get_type(uref, &type))) {
            switch (type) {
                case H264SLI_TYPE_P:
                    frame->pict_type = AV_PICTURE_TYPE_P;
                    break;
                case H264SLI_TYPE_B:
                    frame->pict_type = AV_PICTURE_TYPE_B;
                    break;
                case H264SLI_TYPE_I:
                    frame->pict_type = AV_PICTURE_TYPE_I;
                    break;
                case H264SLI_TYPE_SP:
                    frame->pict_type = AV_PICTURE_TYPE_SP;
                    break;
                case H264SLI_TYPE_SI:
                    frame->pict_type = AV_PICTURE_TYPE_SI;
                    break;
                default:
                    break;
            }
        } else if (ubase_check(uref_h265_get_type(uref, &type))) {
            switch (type) {
                case H265SLI_TYPE_P:
                    frame->pict_type = AV_PICTURE_TYPE_P;
                    break;
                case H265SLI_TYPE_B:
                    frame->pict_type = AV_PICTURE_TYPE_B;
                    break;
                case H265SLI_TYPE_I:
                    frame->pict_type = AV_PICTURE_TYPE_I;
                    break;
                default:
                    break;
            }
        } else if (ubase_check(uref_mpgv_get_type(uref, &type))) {
            switch (type) {
                case MP2VPIC_TYPE_P:
                    frame->pict_type = AV_PICTURE_TYPE_P;
                    break;
                case MP2VPIC_TYPE_B:
                    frame->pict_type = AV_PICTURE_TYPE_B;
                    break;
                case MP2VPIC_TYPE_I:
                    frame->pict_type = AV_PICTURE_TYPE_I;
                    break;
                case MP2VPIC_TYPE_D:
                default:
                    break;
            }
        }
    }

    /* set pts (needed for uref/avpkt mapping) */
    upipe_verbose_va(upipe, "input pts %"PRId64, upipe_avcenc->avcpts);
    frame->pts = upipe_avcenc->avcpts++;
    if (unlikely(!ubase_check(uref_avcenc_set_priv(uref, frame->pts)))) {
        uref_free(uref);
        av_frame_unref(frame);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* store uref in mapping list */
    ulist_add(&upipe_avcenc->urefs_in_use, uref_to_uchain(uref));

    if (unlikely(!ubase_check(upipe_avcenc_encode_frame(upipe, frame,
                                                        upump_p)))) {
        ulist_delete(uref_to_uchain(uref));
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
    }
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

    int size = av_samples_get_buffer_size(NULL, context->ch_layout.nb_channels,
                                          context->frame_size,
                                          context->sample_fmt, 0);

    frame->nb_samples = context->frame_size;
    frame->format = context->sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, &context->ch_layout);

    /* TODO replace with umem */
    uint8_t *buf = malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (unlikely(buf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    avcodec_fill_audio_frame(frame, context->ch_layout.nb_channels,
                             context->sample_fmt, buf, size, 0);

    struct uref *main_uref = NULL;
    size_t offset = 0;
    while (offset < context->frame_size) {
        struct uchain *uchain = ulist_peek(&upipe_avcenc->sound_urefs);
        if (unlikely(uchain == NULL)) {
            /* end of stream, finish with silence */
            av_samples_set_silence(frame->data, offset,
                                   context->frame_size - offset,
                                   context->ch_layout.nb_channels,
                                   context->sample_fmt);
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
        /* cast buffers because av_samples_copy is badly prototyped */
        int err = av_samples_copy(frame->data, (uint8_t **)buffers,
                                  offset, 0, extracted,
                                  context->ch_layout.nb_channels,
                                  context->sample_fmt);
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

    if (unlikely(!ubase_check(upipe_avcenc_encode_frame(upipe, frame,
                                                        upump_p)))) {
        ulist_delete(uref_to_uchain(main_uref));
        uref_free(main_uref);
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
    }

    free(buf);
}

/** @internal @This processes data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the packet was handled
 */
static bool upipe_avcenc_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    const char *def;
    if (unlikely(uref != NULL && ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_avcenc->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_avcenc->input_latency);
        upipe_avcenc_store_flow_def(upipe, NULL);
        uref_free(upipe_avcenc->flow_def_requested);
        upipe_avcenc->flow_def_requested = NULL;
        uref = upipe_avcenc_store_flow_def_input(upipe, uref);
        if (uref != NULL) {
            uref_pic_flow_clear_format(uref);
            upipe_avcenc_require_flow_format(upipe, uref);
        }
        return true;
    }

    if (upipe_avcenc->flow_def_requested == NULL)
        return false;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(context->pix_fmt);
    if (desc != NULL && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
        AVFrame *frame = av_frame_alloc();
        if (frame == NULL) {
            upipe_err(upipe, "cannot allocate avframe");
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return true;
        }
        int err = ubuf_av_get_avframe(uref->ubuf, frame);
        if (!ubase_check(err)) {
            upipe_err(upipe, "cannot get avframe from uref");
            upipe_throw_error(upipe, err);
            av_frame_free(&frame);
            uref_free(uref);
            return true;
        }
        if (context->hw_frames_ctx != NULL &&
            context->hw_frames_ctx->data != frame->hw_frames_ctx->data) {
            upipe_notice(upipe, "hw frames ctx changed");
            if (context->codec->capabilities & AV_CODEC_CAP_DELAY)
                upipe_avcenc_encode_frame(upipe, NULL, upump_p);
            upipe_avcenc_reinit(upipe);
            if (avcodec_is_open(upipe_avcenc->context)) {
                av_frame_free(&frame);
                return false;
            }
            context = upipe_avcenc->context;
        }
        if (context->hw_frames_ctx == NULL) {
            context->hw_frames_ctx = av_buffer_ref(frame->hw_frames_ctx);
            if (context->hw_frames_ctx == NULL) {
                upipe_err(upipe, "cannot create avframe ref");
                upipe_throw_error(upipe, UBASE_ERR_ALLOC);
                av_frame_free(&frame);
                uref_free(uref);
                return true;
            }
        }
        av_frame_free(&frame);
    }

    while (unlikely(!avcodec_is_open(upipe_avcenc->context))) {
        if (upipe_avcenc->upump_av_deal != NULL)
            return false;

        upipe_avcenc_open(upipe);
    }

    uref_clock_get_rate(uref, &upipe_avcenc->drift_rate);
    uref_clock_get_pts_prog(uref, &upipe_avcenc->input_pts);
    uref_clock_get_pts_sys(uref, &upipe_avcenc->input_pts_sys);

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

        case AVMEDIA_TYPE_SUBTITLE:
            upipe_avcenc_encode_subtitle(upipe, uref, upump_p);
            break;

        default:
            uref_free(uref);
            break;
    }
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_avcenc_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    if (!upipe_avcenc_check_input(upipe)) {
        upipe_avcenc_hold_input(upipe, uref);
        upipe_avcenc_block_input(upipe, upump_p);
    } else if (!upipe_avcenc_handle(upipe, uref, upump_p)) {
        upipe_avcenc_hold_input(upipe, uref);
        upipe_avcenc_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
        upipe_avcenc->release_needed = true;
        upipe_use(upipe);
    }
}

/** @internal @This builds the flow definition attributes packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avcenc_build_flow_def_attr(struct upipe *upipe)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    AVCodecContext *context = upipe_avcenc->context;
    const AVCodec *codec = context->codec;
    struct uref *flow_def_attr = uref_dup(upipe_avcenc->flow_def_attr);
    if (unlikely(flow_def_attr == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    const char *codec_def = upipe_av_to_flow_def(codec->id);
    UBASE_FATAL(upipe, uref_flow_set_def_va(flow_def_attr, PREFIX_FLOW "%s",
                                      codec_def));
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def_attr))

    /* Find out if flow def attributes have changed. */
    if (!upipe_avcenc_check_flow_def_attr(upipe, flow_def_attr)) {
        upipe_avcenc_store_flow_def(upipe, NULL);
        uref_free(upipe_avcenc->flow_def_requested);
        upipe_avcenc->flow_def_requested = NULL;
        struct uref *flow_def =
            upipe_avcenc_store_flow_def_attr(upipe, flow_def_attr);
        if (flow_def != NULL) {
            uref_pic_flow_clear_format(flow_def);
            upipe_avcenc_require_flow_format(upipe, flow_def);
        }
    } else
        uref_free(flow_def_attr);
}

/** @internal @This receives the result of a flow format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_avcenc_check_flow_format(struct upipe *upipe,
                                          struct uref *flow_format)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    if (ubase_check(uref_flow_get_global(flow_format)))
        upipe_avcenc->context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    else
        upipe_avcenc->context->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;

    if (!strcmp(upipe_avcenc->context->codec->name, "libfdk_aac")) {
        enum uref_mpga_encaps encaps = uref_mpga_flow_infer_encaps(flow_format);
        switch (encaps) {
            default:
            case UREF_MPGA_ENCAPS_ADTS:
                upipe_avcenc_set_option(upipe, "latm", "0");
                break;
            case UREF_MPGA_ENCAPS_LOAS:
                upipe_avcenc_set_option(upipe, "latm", "1");
                break;
            case UREF_MPGA_ENCAPS_RAW:
                upipe_avcenc->context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                upipe_avcenc_set_option(upipe, "latm", "0");
                break;
        }
        uint8_t signaling;
        if (ubase_check(uref_mpga_flow_get_signaling(flow_format, &signaling)))
            switch (signaling) {
                default:
                case UREF_MPGA_SIGNALING_AUTO:
                    upipe_avcenc_set_option(upipe, "signaling",
                                            "default");
                    break;
                case UREF_MPGA_SIGNALING_IMPLICIT:
                    upipe_avcenc_set_option(upipe, "signaling",
                                            "implicit");
                    break;
                case UREF_MPGA_SIGNALING_EXPLICIT_COMPATIBLE:
                    upipe_avcenc_set_option(upipe, "signaling",
                                            "explicit_sbr");
                    break;
                case UREF_MPGA_SIGNALING_EXPLICIT_HIERARCHICAL:
                    upipe_avcenc_set_option(upipe, "signaling",
                                            "explicit_hierarchical");
                    break;
            }
    }

    uref_free(upipe_avcenc->flow_def_requested);
    upipe_avcenc->flow_def_requested = NULL;
    upipe_avcenc_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_avcenc_check_ubuf_mgr(struct upipe *upipe,
                                       struct uref *flow_format)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_NONE; /* should not happen */

    uref_free(upipe_avcenc->flow_def_requested);
    upipe_avcenc->flow_def_requested = flow_format;
    upipe_avcenc_store_flow_def(upipe, NULL);

    bool was_buffered = !upipe_avcenc_check_input(upipe);
    upipe_avcenc_output_input(upipe);
    upipe_avcenc_unblock_input(upipe);
    if (was_buffered && upipe_avcenc_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_avcenc_input. */
        if (upipe_avcenc->release_needed) {
            upipe_release(upipe);
            upipe_avcenc->release_needed = false;
        }
    }

    return UBASE_ERR_NONE;
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

    if (!ubase_ncmp(def, "pic.sub.")) {
        uint64_t hsize, vsize;
        if (!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
            !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize))) {
            upipe_err(upipe, "incompatible flow def attributes");
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }

    } else if (!ubase_ncmp(def, "pic.")) {
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
                     !ubase_check(uref_pic_flow_set_fps(flow_def_check, fps)) ||
                     !ubase_check(uref_pic_flow_copy_full_range(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_colour_primaries(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_transfer_characteristics(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_matrix_coefficients(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_mdcv(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_max_cll(
                             flow_def_check, flow_def)) ||
                     !ubase_check(uref_pic_flow_copy_max_fall(
                             flow_def_check, flow_def)))) {
            uref_free(flow_def_check);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

    } else if (!ubase_ncmp(def, "sound.")) {
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

    if (upipe_avcenc->flow_def_check != NULL) {
        /* Die if the attributes changed. */
        /* NB: this supposes that all attributes are in the udict, and that
         * the udict is never empty. */
        if (!upipe_avcenc_check_flow_def_check(upipe, flow_def_check)) {
            uref_free(flow_def_check);
            return UBASE_ERR_BUSY;
        }
        uref_free(flow_def_check);

    } else if (!ubase_ncmp(def, "pic.sub.")) {
        uint64_t hsize = 0, vsize = 0;
        uref_pic_flow_get_hsize(flow_def, &hsize);
        uref_pic_flow_get_vsize(flow_def, &vsize);
        uref_pic_flow_get_hsize_visible(flow_def, &hsize);
        uref_pic_flow_get_vsize_visible(flow_def, &vsize);
        context->width = hsize;
        context->height = vsize;

        context->time_base.num = 1;
        context->time_base.den = 1;

        struct urational sar;
        if (ubase_check(uref_pic_flow_get_sar(flow_def, &sar)) && sar.num) {
            context->sample_aspect_ratio.num = sar.num;
            context->sample_aspect_ratio.den = sar.den;
        }

        if (codec->id == AV_CODEC_ID_DVB_SUBTITLE) {
            context->bit_rate = TB_RATE_DVBSUB_DISP * 8;
            context->rc_buffer_size = BS_DVBSUB_DISP * 8;
        }

        upipe_avcenc_store_flow_def_check(upipe, flow_def_check);

    } else if (!ubase_ncmp(def, "pic.")) {
        uint64_t hsize = 0, vsize = 0;
        uref_pic_flow_get_hsize(flow_def, &hsize);
        uref_pic_flow_get_vsize(flow_def, &vsize);
        context->width = hsize;
        context->height = vsize;
        context->pix_fmt = upipe_av_pixfmt_from_flow_def(
            flow_def, codec->pix_fmts, upipe_avcenc->chroma_map);

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
        context->framerate.num = fps.num;
        context->framerate.den = fps.den;

        struct urational sar;
        if (ubase_check(uref_pic_flow_get_sar(flow_def, &sar)) && sar.num) {
            context->sample_aspect_ratio.num = sar.num;
            context->sample_aspect_ratio.den = sar.den;
        }

        context->color_range =
            ubase_check(uref_pic_flow_get_full_range(flow_def)) ?
            AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

        int val;
        if (ubase_check(
                uref_pic_flow_get_colour_primaries_val(flow_def, &val)))
            context->color_primaries = val;
        if (ubase_check(
                uref_pic_flow_get_transfer_characteristics_val(flow_def, &val)))
            context->color_trc = val;
        if (ubase_check(
                uref_pic_flow_get_matrix_coefficients_val(flow_def, &val)))
            context->colorspace = val;

        if (!ubase_check(uref_pic_get_progressive(flow_def))) {
            context->flags |= AV_CODEC_FLAG_INTERLACED_DCT |
                              AV_CODEC_FLAG_INTERLACED_ME;
            if (ubase_check(uref_pic_get_tff(flow_def)))
                context->field_order = AV_FIELD_TT;
            else
                context->field_order = AV_FIELD_BB;
        }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 2, 100)
        uint64_t max_cll;
        uint64_t max_fall;
        if (ubase_check(uref_pic_flow_get_max_cll(flow_def, &max_cll)) &&
            ubase_check(uref_pic_flow_get_max_fall(flow_def, &max_fall))) {
            AVFrameSideData *sd = av_frame_side_data_new(
                &context->decoded_side_data,
                &context->nb_decoded_side_data,
                AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
                sizeof(AVContentLightMetadata),
                AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
            if (!sd) {
                uref_free(flow_def_check);
                return UBASE_ERR_EXTERNAL;
            }
            AVContentLightMetadata *clm =
                (AVContentLightMetadata *)sd->data;
            clm->MaxCLL = max_cll;
            clm->MaxFALL = max_fall;
        }

        struct uref_pic_mastering_display mdcv;
        if (ubase_check(uref_pic_flow_get_mastering_display(flow_def, &mdcv))) {
            AVFrameSideData *sd = av_frame_side_data_new(
                &context->decoded_side_data,
                &context->nb_decoded_side_data,
                AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,
                sizeof(AVMasteringDisplayMetadata),
                AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
            if (!sd) {
                uref_free(flow_def_check);
                return UBASE_ERR_EXTERNAL;
            }
            AVMasteringDisplayMetadata *mdm =
                (AVMasteringDisplayMetadata *)sd->data;
            int chroma = 50000;
            int luma = 10000;
            mdm->display_primaries[0][0] = av_make_q(mdcv.red_x, chroma);
            mdm->display_primaries[0][1] = av_make_q(mdcv.red_y, chroma);
            mdm->display_primaries[1][0] = av_make_q(mdcv.green_x, chroma);
            mdm->display_primaries[1][1] = av_make_q(mdcv.green_y, chroma);
            mdm->display_primaries[2][0] = av_make_q(mdcv.blue_x, chroma);
            mdm->display_primaries[2][1] = av_make_q(mdcv.blue_y, chroma);
            mdm->white_point[0] = av_make_q(mdcv.white_x, chroma);
            mdm->white_point[1] = av_make_q(mdcv.white_y, chroma);
            mdm->has_primaries = 1;
            mdm->max_luminance = av_make_q(mdcv.max_luminance, luma);
            mdm->min_luminance = av_make_q(mdcv.min_luminance, luma);
            mdm->has_luminance = 1;
        }
#endif

        upipe_avcenc_store_flow_def_check(upipe, flow_def_check);

    } else {
        const enum AVSampleFormat *sample_fmts = codec->sample_fmts;
        if (sample_fmts == NULL) {
            upipe_err_va(upipe, "unknown sample format %s", def);
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        while (*sample_fmts != -1) {
            if (ubase_check(upipe_av_samplefmt_match_flow_def(flow_def,
                                                              *sample_fmts)))
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

        uint8_t channels;
        if (!ubase_check(uref_sound_flow_get_channels(flow_def, &channels))) {
            upipe_err_va(upipe, "unsupported channels");
            uref_free(flow_def_check);
            return UBASE_ERR_INVALID;
        }
        const AVChannelLayout *ch_layouts = context->codec->ch_layouts;
        if (ch_layouts) {
            while (ch_layouts->nb_channels != 0) {
                if (ch_layouts->nb_channels == channels)
                    break;
                ch_layouts++;
            }
            if (ch_layouts->nb_channels == 0) {
                upipe_err_va(upipe, "unsupported channel layout %"PRIu8,
                             channels);
                uref_free(flow_def_check);
                return UBASE_ERR_INVALID;
            }
            av_channel_layout_copy(&context->ch_layout, ch_layouts);
        } else {
            av_channel_layout_default(&context->ch_layout, channels);
        }

        upipe_avcenc_store_flow_def_check(upipe, flow_def_check);
    }

    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the hardware pixel format for the hw_frames
 * context.
 *
 * @param codec the codec description
 * @return the hardware pixel format
 */
static enum AVPixelFormat upipe_avcenc_get_hw_pix_fmt(const AVCodec *codec)
{
    const AVCodecHWConfig *config;
    int i = 0;

    while ((config = avcodec_get_hw_config(codec, i++)) != NULL)
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX)
            return config->pix_fmt;

    return AV_PIX_FMT_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int _upipe_avcenc_provide_flow_format(struct upipe *upipe,
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

    if (!ubase_ncmp(def, "pic.sub.")) {
        if (codec->type != AVMEDIA_TYPE_SUBTITLE ||
            codec->id != AV_CODEC_ID_DVB_SUBTITLE)
            goto upipe_avcenc_provide_flow_format_err;
        return urequest_provide_flow_format(request, flow_format);

    } else if (!ubase_ncmp(def, "pic.")) {
        if (unlikely(codec->pix_fmts == NULL || codec->pix_fmts[0] == -1))
            goto upipe_avcenc_provide_flow_format_err;

        enum AVPixelFormat hw_pix_fmt = upipe_avcenc_get_hw_pix_fmt(codec);
        if (hw_pix_fmt != AV_PIX_FMT_NONE) {
            const char *chroma_map[UPIPE_AV_MAX_PLANES];
            enum AVPixelFormat pix_fmt = upipe_av_sw_pixfmt_from_flow_def(
                flow_format, codec->pix_fmts, chroma_map);
            if (pix_fmt == AV_PIX_FMT_NONE) {
                int bit_depth = 0;
                uref_pic_flow_get_bit_depth(flow_format, &bit_depth);
                uref_pic_flow_clear_format(flow_format);
                if (unlikely(!ubase_check(upipe_av_pixfmt_to_flow_def(
                                bit_depth == 10 ? AV_PIX_FMT_P010 :
                                AV_PIX_FMT_NV12, flow_format))))
                    goto upipe_avcenc_provide_flow_format_err;
            }
            uref_pic_flow_set_surface_type_va(flow_format, "av.%s",
                                              av_get_pix_fmt_name(hw_pix_fmt));
        } else {
            const char *chroma_map[UPIPE_AV_MAX_PLANES];
            enum AVPixelFormat pix_fmt = upipe_av_pixfmt_from_flow_def(flow_format,
                        codec->pix_fmts, chroma_map);
            if (pix_fmt == AV_PIX_FMT_NONE) {
                uref_pic_flow_clear_format(flow_format);
                if (unlikely(!ubase_check(upipe_av_pixfmt_to_flow_def(
                                codec->pix_fmts[0], flow_format))))
                    goto upipe_avcenc_provide_flow_format_err;
            }
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

        const AVChannelLayout *ch_layouts = codec->ch_layouts;
        if (ch_layouts != NULL) {
            const AVChannelLayout *selected_layout = NULL;
            uint64_t diff_channels = UINT64_MAX; /* arbitrarily big */
            while (ch_layouts->nb_channels) {
                uint8_t this_channels = ch_layouts->nb_channels;
                if (this_channels == channels) {
                    selected_layout = ch_layouts;
                    break;
                }

                uint8_t this_diff_channels = this_channels > channels ?
                                             this_channels - channels :
                                             channels - this_channels;
                if (this_diff_channels < diff_channels) {
                    diff_channels = this_diff_channels;
                    selected_layout = ch_layouts;
                }
                ch_layouts++;
            }

            if (selected_layout == NULL)
                goto upipe_avcenc_provide_flow_format_err;
            channels = selected_layout->nb_channels;
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
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     av_err2str(error));
        return UBASE_ERR_EXTERNAL;
    }

    if (content != NULL)
        udict_set_string(upipe_avcenc->options->udict, content,
                         UDICT_TYPE_STRING, option);
    else
        udict_delete(upipe_avcenc->options->udict, UDICT_TYPE_STRING, option);

    upipe_avcenc_build_flow_def_attr(upipe);
    return UBASE_ERR_NONE;
}

/** @This sets the slice type enforcement mode (true or false).
 *
 * @param upipe description structure of the pipe
 * @param enforce true if the incoming slice types must be enforced
 * @return an error code
 */
static int _upipe_avcenc_set_slice_type_enforce(struct upipe *upipe,
                                                bool enforce)
{
    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    upipe_avcenc->slice_type_enforce = enforce;
    upipe_dbg_va(upipe, "%sactivating slice type enforcement",
                 enforce ? "" : "de");
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
                return _upipe_avcenc_provide_flow_format(upipe, request);
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

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_avcenc_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_avcenc_control_output(upipe, command, args);

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_avcenc_set_option(upipe, option, content);
        }
        case UPIPE_AVCENC_SET_SLICE_TYPE_ENFORCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AVCENC_SIGNATURE)
            bool enforce = va_arg(args, int) != 0;
            return _upipe_avcenc_set_slice_type_enforce(upipe, enforce);
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
    av_frame_free(&upipe_avcenc->frame);
    av_packet_free(&upipe_avcenc->avpkt);

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
    uref_free(upipe_avcenc->flow_def_requested);
    uref_free(upipe_avcenc->options);
    upipe_avcenc_abort_av_deal(upipe);
    upipe_avcenc_clean_input(upipe);
    upipe_avcenc_clean_ubuf_mgr(upipe);
    upipe_avcenc_clean_upump_av_deal(upipe);
    upipe_avcenc_clean_upump_mgr(upipe);
    upipe_avcenc_clean_output(upipe);
    upipe_avcenc_clean_flow_format(upipe);
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
    AVFrame *frame = av_frame_alloc();
    if (unlikely(frame == NULL))
        return NULL;

    AVPacket *avpkt = av_packet_alloc();
    if (unlikely(avpkt == NULL)) {
        av_frame_free(&frame);
        return NULL;
    }

    struct uref *flow_def;
    struct upipe *upipe = upipe_avcenc_alloc_flow(mgr, uprobe, signature, args,
                                                  &flow_def);
    if (unlikely(upipe == NULL)) {
        av_frame_free(&frame);
        av_packet_free(&avpkt);
        return NULL;
    }

    struct uref *options = uref_alloc_control(flow_def->mgr);
    if (options == NULL) {
        av_frame_free(&frame);
        av_packet_free(&avpkt);
        upipe_avcenc_free_flow(upipe);
        return NULL;
    }

    struct upipe_avcenc *upipe_avcenc = upipe_avcenc_from_upipe(upipe);
    const char *def, *name;
    enum AVCodecID codec_id;
    const AVCodec *codec = NULL;

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
        av_frame_free(&frame);
        av_packet_free(&avpkt);
        uref_free(options);
        upipe_avcenc_free_flow(upipe);
        return NULL;
    }

    upipe_avcenc->frame = frame;
    upipe_avcenc->avpkt = avpkt;
    upipe_avcenc->context->codec = codec;
    upipe_avcenc->context->opaque = upipe;

    upipe_avcenc_init_urefcount(upipe);
    upipe_avcenc_init_ubuf_mgr(upipe);
    upipe_avcenc_init_upump_mgr(upipe);
    upipe_avcenc_init_upump_av_deal(upipe);
    upipe_avcenc_init_output(upipe);
    upipe_avcenc_init_input(upipe);
    upipe_avcenc_init_flow_format(upipe);
    upipe_avcenc_init_flow_def(upipe);
    upipe_avcenc_init_flow_def_check(upipe);
    upipe_avcenc_store_flow_def_attr(upipe, flow_def);
    upipe_avcenc->flow_def_requested = NULL;
    upipe_avcenc->slice_type_enforce = false;
    upipe_avcenc->options = options;
    upipe_avcenc->release_needed = false;

    ulist_init(&upipe_avcenc->sound_urefs);
    upipe_avcenc->nb_samples = 0;

    ulist_init(&upipe_avcenc->urefs_in_use);
    upipe_avcenc->avcpts = AVCPTS_INIT;
    upipe_avcenc->last_pts = UINT64_MAX;
    upipe_avcenc->last_dts = UINT64_MAX;
    upipe_avcenc->last_dts_sys = UINT64_MAX;
    upipe_avcenc->drift_rate.num = upipe_avcenc->drift_rate.den = 1;
    upipe_avcenc->input_pts = UINT64_MAX;
    upipe_avcenc->input_pts_sys = UINT64_MAX;
    upipe_avcenc->input_latency = 0;

    upipe_throw_ready(upipe);
    upipe_avcenc_build_flow_def_attr(upipe);
    return upipe;
}

/** @This configures the given flow definition to be able to encode to the
 * av codec described by name.
 *
 * @param flow_def flow definition packet
 * @param name codec name
 * @return an error code
 */
static int _upipe_avcenc_mgr_set_flow_def_from_name(struct uref *flow_def,
                                                    const char *name)
{
    if (name == NULL)
        return UBASE_ERR_INVALID;
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
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
