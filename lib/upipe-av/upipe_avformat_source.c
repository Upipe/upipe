/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe source module libavformat wrapper
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-modules/upipe_proxy.h>

#include "upipe_av_internal.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

#include <libavutil/dict.h>
#include <libavformat/avformat.h>

/** lowest possible timestamp (just an arbitrarily high time) */
#define AV_CLOCK_MIN UINT32_MAX
/** offset between DTS and (artificial) clock references */
#define PCR_OFFSET UCLOCK_FREQ

/** @internal @This is the private context of an avformat source pipe. */
struct upipe_avfsrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** offset between libavformat timestamps and Upipe timestamps */
    int64_t timestamp_offset;
    /** highest Upipe timestamp given to a frame */
    uint64_t timestamp_highest;
    /** last random access point */
    uint64_t systime_rap;

    /** list of subs */
    struct ulist subs;

    /** URL */
    char *url;

    /** avcodec initialization watcher */
    struct upump *upump_av_deal;
    /** avformat options */
    AVDictionary *options;
    /** avformat context opened from URL */
    AVFormatContext *context;
    /** true if the URL has already been probed by avformat */
    bool probed;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsrc, upipe)
UPIPE_HELPER_VOID(upipe_avfsrc)
UPIPE_HELPER_UREF_MGR(upipe_avfsrc, uref_mgr)

UPIPE_HELPER_UPUMP_MGR(upipe_avfsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_avfsrc, upump, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_avfsrc, uclock)

/** @internal @This is the private context of an output of an avformat source
 * pipe. */
struct upipe_avfsrc_sub {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** libavformat stream ID */
    uint64_t id;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition */
    struct uref *flow_def;
    /** true if the flow definition has been sent */
    bool flow_def_sent;
    /** ubuf manager for this output */
    struct ubuf_mgr *ubuf_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsrc_sub, upipe)
UPIPE_HELPER_FLOW(upipe_avfsrc_sub, NULL)
UPIPE_HELPER_OUTPUT(upipe_avfsrc_sub, output, flow_def, flow_def_sent)
UPIPE_HELPER_UBUF_MGR(upipe_avfsrc_sub, ubuf_mgr)

UPIPE_HELPER_SUBPIPE(upipe_avfsrc, upipe_avfsrc_sub, sub, sub_mgr,
                     subs, uchain)

/** @internal @This allocates an output subpipe of an avfsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsrc_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_avfsrc_sub_alloc_flow(mgr, uprobe, signature,
                                                      args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_avfsrc_sub *upipe_avfsrc_sub =
        upipe_avfsrc_sub_from_upipe(upipe);
    upipe_avfsrc_sub->id = UINT64_MAX;
    upipe_avfsrc_sub_init_output(upipe);
    upipe_avfsrc_sub_init_ubuf_mgr(upipe);

    upipe_avfsrc_sub_init_sub(upipe);

    struct upipe_avfsrc *upipe_avfsrc =
        upipe_avfsrc_from_sub_mgr(upipe->mgr);
    upipe_use(upipe_avfsrc_to_upipe(upipe_avfsrc));
    upipe_throw_ready(upipe);

    uint64_t id;
    if (unlikely(!uref_av_flow_get_id(flow_def, &id))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return upipe;
    }

    /* check that the ID is not already in use */
    struct uchain *uchain;
    ulist_foreach(&upipe_avfsrc->subs, uchain) {
        struct upipe_avfsrc_sub *output =
            upipe_avfsrc_sub_from_uchain(uchain);
        if (output != upipe_avfsrc_sub && output->id == id) {
            upipe_warn_va(upipe, "ID %"PRIu64" is already in use", id);
            uref_free(flow_def);
            return upipe;
        }
    }

    upipe_avfsrc_sub->id = id;
    upipe_avfsrc_sub_store_flow_def(upipe, flow_def);
    return upipe;
}

/** @internal @This processes control commands on an output subpipe of an avfsrc
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_avfsrc_sub_control(struct upipe *upipe,
                                     enum upipe_command command,
                                     va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_avfsrc_sub_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_avfsrc_sub_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_avfsrc_sub_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avfsrc_sub_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_avfsrc_sub_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_sub_free(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc =
        upipe_avfsrc_from_sub_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_avfsrc_sub_clean_ubuf_mgr(upipe);
    upipe_avfsrc_sub_clean_output(upipe);
    upipe_avfsrc_sub_clean_sub(upipe);
    upipe_avfsrc_sub_free_flow(upipe);

    upipe_release(upipe_avfsrc_to_upipe(upipe_avfsrc));
}

/** @internal @This initializes the output manager for an avfsrc pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_avfsrc->sub_mgr;
    sub_mgr->signature = UPIPE_AVFSRC_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_avfsrc_sub_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_avfsrc_sub_control;
    sub_mgr->upipe_free = upipe_avfsrc_sub_free;
    sub_mgr->upipe_mgr_free = NULL;
}

/** @internal @This allocates an avfsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_avfsrc_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    upipe_avfsrc_init_sub_mgr(upipe);
    upipe_avfsrc_init_sub_subs(upipe);
    upipe_avfsrc_init_uref_mgr(upipe);
    upipe_avfsrc_init_upump_mgr(upipe);
    upipe_avfsrc_init_upump(upipe);
    upipe_avfsrc_init_uclock(upipe);
    upipe_avfsrc->timestamp_offset = 0;
    upipe_avfsrc->timestamp_highest = AV_CLOCK_MIN;
    upipe_avfsrc->systime_rap = UINT64_MAX;

    upipe_avfsrc->url = NULL;

    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->options = NULL;
    upipe_avfsrc->context = NULL;
    upipe_avfsrc->probed = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This aborts and frees an existing upump watching for exclusive access to
 * avcodec_open().
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_abort_av_deal(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (unlikely(upipe_avfsrc->upump_av_deal != NULL)) {
        upipe_av_deal_abort(upipe_avfsrc->upump_av_deal);
        upump_free(upipe_avfsrc->upump_av_deal);
        upipe_avfsrc->upump_av_deal = NULL;
    }
}

/** @internal @This reads data from the source and subs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the file descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static struct upipe_avfsrc_sub *upipe_avfsrc_find_output(struct upipe *upipe,
                                                            uint64_t id)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach(&upipe_avfsrc->subs, uchain) {
        struct upipe_avfsrc_sub *output =
            upipe_avfsrc_sub_from_uchain(uchain);
        if (output->id == id)
            return output;
    }
    return NULL;
}

/** @internal @This reads data from the source and subs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the file descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_avfsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVPacket pkt;

    int error = av_read_frame(upipe_avfsrc->context, &pkt);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "read error from %s (%s)", upipe_avfsrc->url, buf);
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    struct upipe_avfsrc_sub *output =
        upipe_avfsrc_find_output(upipe, pkt.stream_index);
    if (output == NULL) {
        av_free_packet(&pkt);
        return;
    }
    if (unlikely(output->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe_avfsrc_sub_to_upipe(output),
                                  output->flow_def);
    if (unlikely(output->ubuf_mgr == NULL)) {
        av_free_packet(&pkt);
        return;
    }

    struct uref *uref = uref_block_alloc(upipe_avfsrc->uref_mgr,
                                         output->ubuf_mgr, pkt.size);
    if (unlikely(uref == NULL)) {
        av_free_packet(&pkt);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    AVStream *stream = upipe_avfsrc->context->streams[pkt.stream_index];
    uint64_t systime = upipe_avfsrc->uclock != NULL ?
                       uclock_now(upipe_avfsrc->uclock) : 0;
    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!uref_block_write(uref, 0, &read_size, &buffer))) {
        uref_free(uref);
        av_free_packet(&pkt);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    assert(read_size == pkt.size);
    memcpy(buffer, pkt.data, pkt.size);
    uref_block_unmap(uref, 0);
    av_free_packet(&pkt);

    bool ret = true, ts = false;;
    if (upipe_avfsrc->uclock != NULL)
        ret = ret && uref_clock_set_systime(uref, systime);
    if (pkt.dts != (int64_t)AV_NOPTS_VALUE) {
        uint64_t dts_orig = pkt.dts * stream->time_base.num * UCLOCK_FREQ /
                            stream->time_base.den;
        ret = ret && uref_clock_set_dts_orig(uref, dts_orig);

        if (!upipe_avfsrc->timestamp_offset)
            upipe_avfsrc->timestamp_offset = upipe_avfsrc->timestamp_highest -
                                             dts_orig + PCR_OFFSET;
        uint64_t dts = dts_orig + upipe_avfsrc->timestamp_offset;
        ret = ret && uref_clock_set_dts(uref, dts);
        if (upipe_avfsrc->timestamp_highest < dts)
            upipe_avfsrc->timestamp_highest = dts;
        ts = true;

        if (upipe_avfsrc->uclock != NULL && stream->reference_dts == pkt.dts)
            upipe_avfsrc->systime_rap = systime;

        /* this is subtly wrong, but whatever */
        upipe_throw_clock_ref(upipe, uref,
                              dts + upipe_avfsrc->timestamp_offset - PCR_OFFSET,
                              0);
    }
    if (pkt.pts != (int64_t)AV_NOPTS_VALUE) {
        uint64_t pts_orig = pkt.pts * stream->time_base.num * UCLOCK_FREQ /
                            stream->time_base.den;
        ret = ret && uref_clock_set_pts_orig(uref, pts_orig);

        if (!upipe_avfsrc->timestamp_offset)
            upipe_avfsrc->timestamp_offset = upipe_avfsrc->timestamp_highest -
                                             pts_orig + PCR_OFFSET;
        uint64_t pts = pts_orig + upipe_avfsrc->timestamp_offset;
        ret = ret && uref_clock_set_pts(uref, pts);
        if (upipe_avfsrc->timestamp_highest < pts)
            upipe_avfsrc->timestamp_highest = pts;
        ts = true;
    }
    if (pkt.duration > 0) {
        uint64_t duration = pkt.duration * stream->time_base.num * UCLOCK_FREQ /
                            stream->time_base.den;
        ret = ret && uref_clock_set_duration(uref, duration);
    } else
        upipe_warn(upipe, "packet without duration");
    if (upipe_avfsrc->systime_rap != UINT64_MAX)
        ret = ret && uref_clock_set_systime_rap(uref,
                                                upipe_avfsrc->systime_rap);

    if (ts)
        upipe_throw_clock_ts(upipe, uref);

    upipe_avfsrc_sub_output(upipe_avfsrc_sub_to_upipe(output), uref, upump);
}

/** @internal @This starts the worker.
 *
 * @param upipe description structure of the pipe
 */
static bool upipe_avfsrc_start(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    struct upump *upump = upump_alloc_idler(upipe_avfsrc->upump_mgr,
                                            upipe_avfsrc_worker, upipe);
    if (unlikely(upump == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
        return false;
    }
    upipe_avfsrc_set_upump(upipe, upump);
    upump_start(upump);
    return true;
}

/** @hidden */
#define CHK(x)                                                              \
    if (unlikely(!x))                                                       \
        return NULL;

/** @internal @This returns a flow definition for a raw audio media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_audio_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    if (unlikely(codec->bits_per_coded_sample % 8))
        return NULL;

    struct uref *flow_def =
        uref_sound_flow_alloc_def(uref_mgr, codec->channels,
                                  codec->bits_per_coded_sample / 8);
    if (unlikely(flow_def == NULL))
        return NULL;

    CHK(uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->block_align)
        CHK(uref_sound_flow_set_samples(flow_def,
                                        codec->block_align /
                                        (codec->bits_per_coded_sample / 8) /
                                        codec->channels))
    return flow_def;
}

/** @internal @This returns a flow definition for a coded audio media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_audio_def(struct uref_mgr *uref_mgr,
                                    AVCodecContext *codec)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def =
        uref_block_flow_alloc_def_va(uref_mgr, "%s", def);
    if (unlikely(flow_def == NULL))
        return NULL;

    if (codec->bit_rate)
        CHK(uref_block_flow_set_octetrate(flow_def, (codec->bit_rate + 7) / 8))

    CHK(uref_sound_flow_set_channels(flow_def, codec->channels))
    CHK(uref_sound_flow_set_rate(flow_def, codec->sample_rate))
    if (codec->block_align)
        CHK(uref_block_flow_set_size(flow_def, codec->block_align))
    return flow_def;
}

/** @internal @This returns a flow definition for a raw video media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_raw_video_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

/** @internal @This returns a flow definition for a coded video media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_video_def(struct uref_mgr *uref_mgr,
                                    AVCodecContext *codec)
{
    const char *def = upipe_av_to_flow_def(codec->codec_id);
    if (unlikely(def == NULL))
        return NULL;

    struct uref *flow_def = uref_block_flow_alloc_def_va(uref_mgr, "%s", def);
    if (unlikely(flow_def == NULL))
        return NULL;

    if (codec->bit_rate)
        CHK(uref_block_flow_set_octetrate(flow_def, (codec->bit_rate + 7) / 8))

    CHK(uref_pic_set_hsize(flow_def, codec->width))
    CHK(uref_pic_set_vsize(flow_def, codec->height))
    int ticks = codec->ticks_per_frame ? codec->ticks_per_frame : 1;
    if (codec->time_base.den) {
        struct urational fps = { .num = codec->time_base.num * ticks,
                                 .den = codec->time_base.den };
        urational_simplify(&fps);
        CHK(uref_pic_flow_set_fps(flow_def, fps))
    }
    return flow_def;
}

/** @internal @This returns a flow definition for a subtitles media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_subtitles_def(struct uref_mgr *uref_mgr,
                                        AVCodecContext *codec)
{
    /* TODO */
    /* FIXME extradata */
    return NULL;
}

/** @internal @This returns a flow definition for a data media type.
 *
 * @param uref_mgr uref management structure
 * @param codec avcodec context
 * @return pointer to uref control packet, or NULL in case of error
 */
static struct uref *alloc_data_def(struct uref_mgr *uref_mgr,
                                   AVCodecContext *codec)
{
    /* TODO */
    return NULL;
}

#undef CHK

/** @internal @This probes all flows from the source.
 *
 * @param upump description structure of the dealer
 */
static void upipe_avfsrc_probe(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    AVFormatContext *context = upipe_avfsrc->context;

    if (unlikely(!upipe_av_deal_grab()))
        return;

    AVDictionary *options[context->nb_streams];
    for (unsigned i = 0; i < context->nb_streams; i++) {
        options[i] = NULL;
        av_dict_copy(&options[i], upipe_avfsrc->options, 0);
    }
    int error = avformat_find_stream_info(context, options);

    upipe_av_deal_yield(upump);
    upump_free(upipe_avfsrc->upump_av_deal);
    upipe_avfsrc->upump_av_deal = NULL;
    upipe_avfsrc->probed = true;

    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't probe URL %s (%s)", upipe_avfsrc->url, buf);
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        free(upipe_avfsrc->url);
        upipe_avfsrc->url = NULL;
        return;
    }

    for (int i = 0; i < context->nb_streams; i++) {
        AVStream *stream = context->streams[i];
        AVCodecContext *codec = stream->codec;
        struct uref *flow_def;
        bool ret;

        // discard all packets from this stream unless add_flow is throwed
        stream->discard = AVDISCARD_ALL; 

        switch (codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (codec->codec_id >= CODEC_ID_FIRST_AUDIO &&
                    codec->codec_id < CODEC_ID_ADPCM_IMA_QT)
                    flow_def = alloc_raw_audio_def(upipe_avfsrc->uref_mgr,
                                                   codec);
                else
                    flow_def = alloc_audio_def(upipe_avfsrc->uref_mgr, codec);
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (codec->codec_id == CODEC_ID_RAWVIDEO)
                    flow_def = alloc_raw_video_def(upipe_avfsrc->uref_mgr, codec);
                else
                    flow_def = alloc_video_def(upipe_avfsrc->uref_mgr, codec);
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                flow_def = alloc_subtitles_def(upipe_avfsrc->uref_mgr, codec);
                break;
            default:
                flow_def = alloc_data_def(upipe_avfsrc->uref_mgr, codec);
                break;
        }

        if (unlikely(flow_def == NULL)) {
            upipe_warn_va(upipe, "unsupported track type (%u:%u)",
                          codec->codec_type, codec->codec_id);
            continue;
        }
        ret = uref_av_flow_set_id(flow_def, i);

        AVDictionaryEntry *lang = av_dict_get(stream->metadata, "language",
                                              NULL, 0);
        if (lang != NULL && lang->value != NULL)
            ret = uref_flow_set_lang(flow_def, lang->value) && ret;

        if (unlikely(!ret)) {
            uref_free(flow_def);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        stream->discard = AVDISCARD_DEFAULT; 
        upipe_split_throw_add_flow(upipe, i, flow_def);
        uref_free(flow_def);
    }

    upipe_avfsrc_start(upipe);
}


/** @internal @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return false in case of error
 */
static bool _upipe_avfsrc_get_option(struct upipe *upipe, const char *option,
                                     const char **content_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    assert(content_p != NULL);
    AVDictionaryEntry *entry = av_dict_get(upipe_avfsrc->options, option,
                                           NULL, 0);
    if (unlikely(entry == NULL))
        return false;
    *content_p = entry->value;
    return true;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_avfsrc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool _upipe_avfsrc_set_option(struct upipe *upipe, const char *option,
                                     const char *content)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(option != NULL);
    int error = av_dict_set(&upipe_avfsrc->options, option, content, 0);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
        return false;
    }
    return true;
}

/** @internal @This returns the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the URL
 * @return false in case of error
 */
static bool upipe_avfsrc_get_uri(struct upipe *upipe, const char **url_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(url_p != NULL);
    *url_p = upipe_avfsrc->url;
    return true;
}

/** @internal @This asks to open the given URL.
 *
 * @param upipe description structure of the pipe
 * @param url URL to open
 * @return false in case of error
 */
static bool upipe_avfsrc_set_uri(struct upipe *upipe, const char *url)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);

    if (unlikely(upipe_avfsrc->context != NULL)) {
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        avformat_close_input(&upipe_avfsrc->context);
        upipe_avfsrc->context = NULL;
        upipe_avfsrc_set_upump(upipe, NULL);
        upipe_avfsrc_abort_av_deal(upipe);
    }
    free(upipe_avfsrc->url);
    upipe_avfsrc->url = NULL;

    if (unlikely(url == NULL))
        return true;

    if (upipe_avfsrc->uref_mgr == NULL) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_avfsrc->uref_mgr == NULL))
            return false;
    }
    if (upipe_avfsrc->upump_mgr == NULL)
        upipe_throw_need_upump_mgr(upipe);

    AVDictionary *options = NULL;
    av_dict_copy(&options, upipe_avfsrc->options, 0);
    int error = avformat_open_input(&upipe_avfsrc->context, url, NULL,
                                    &options);
    av_dict_free(&options);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't open URL %s (%s)", url, buf);
        return false;
    }

    upipe_avfsrc->timestamp_offset = 0;
    upipe_avfsrc->url = strdup(url);
    upipe_notice_va(upipe, "opening URL %s", upipe_avfsrc->url);
    return true;
}

/** @internal @This returns the time of the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param time_p filled in with the reading time, in clock units
 * @return false in case of error
 */
static bool _upipe_avfsrc_get_time(struct upipe *upipe, uint64_t *time_p)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    assert(time_p != NULL);
    if (upipe_avfsrc->context != NULL && upipe_avfsrc->context->pb != NULL &&
        upipe_avfsrc->context->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        *time_p = 0; /* TODO */
        return true;
    }
    return false;
}

/** @internal @This asks to read at the given time.
 *
 * @param upipe description structure of the pipe
 * @param time new reading time, in clock units
 * @return false in case of error
 */
static bool _upipe_avfsrc_set_time(struct upipe *upipe, uint64_t time)
{
    //struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    return false;
}

/** @internal @This processes control commands on an avformat source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_avfsrc_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_avfsrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_avfsrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_avfsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_avfsrc_set_upump(upipe, NULL);
            upipe_avfsrc_abort_av_deal(upipe);
            return upipe_avfsrc_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_avfsrc_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            upipe_avfsrc_set_upump(upipe, NULL);
            return upipe_avfsrc_set_uclock(upipe, uclock);
        }

        case UPIPE_AVFSRC_GET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char **content_p = va_arg(args, const char **);
            return _upipe_avfsrc_get_option(upipe, option, content_p);
        }
        case UPIPE_AVFSRC_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avfsrc_set_option(upipe, option, content);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_avfsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_avfsrc_set_uri(upipe, uri);
        }
        case UPIPE_AVFSRC_GET_TIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            uint64_t *time_p = va_arg(args, uint64_t *);
            return _upipe_avfsrc_get_time(upipe, time_p);
        }
        case UPIPE_AVFSRC_SET_TIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSRC_SIGNATURE);
            uint64_t time = va_arg(args, uint64_t);
            return _upipe_avfsrc_set_time(upipe, time);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on an avformat source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_avfsrc_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    if (unlikely(!_upipe_avfsrc_control(upipe, command, args)))
        return false;

    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    if (upipe_avfsrc->upump_mgr != NULL && upipe_avfsrc->url != NULL &&
        upipe_avfsrc->upump == NULL) {
        if (unlikely(upipe_avfsrc->probed))
            return upipe_avfsrc_start(upipe);

        if (unlikely(upipe_avfsrc->upump_av_deal != NULL))
            return true;

        struct upump *upump_av_deal =
            upipe_av_deal_upump_alloc(upipe_avfsrc->upump_mgr,
                                      upipe_avfsrc_probe, upipe);
        if (unlikely(upump_av_deal == NULL)) {
            upipe_err(upipe, "can't create dealer");
            upipe_throw_fatal(upipe, UPROBE_ERR_UPUMP);
            return false;
        }
        upipe_avfsrc->upump_av_deal = upump_av_deal;
        upipe_av_deal_start(upump_av_deal);
    }

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_free(struct upipe *upipe)
{
    struct upipe_avfsrc *upipe_avfsrc = upipe_avfsrc_from_upipe(upipe);
    upipe_avfsrc_clean_sub_subs(upipe);

    upipe_avfsrc_abort_av_deal(upipe);
    if (likely(upipe_avfsrc->context != NULL)) {
        if (likely(upipe_avfsrc->url != NULL))
            upipe_notice_va(upipe, "closing URL %s", upipe_avfsrc->url);
        
        /* Throw del_flow for non-discarded streams. */
        for (int i = 0; i < upipe_avfsrc->context->nb_streams; i++) {
            if (upipe_avfsrc->context->streams[i]->discard != AVDISCARD_ALL) {
                upipe_split_throw_del_flow(upipe, i);
            }
        }

        avformat_close_input(&upipe_avfsrc->context);
    }
    upipe_throw_dead(upipe);

    av_dict_free(&upipe_avfsrc->options);
    free(upipe_avfsrc->url);

    upipe_avfsrc_clean_uclock(upipe);
    upipe_avfsrc_clean_upump(upipe);
    upipe_avfsrc_clean_upump_mgr(upipe);
    upipe_avfsrc_clean_uref_mgr(upipe);
    upipe_avfsrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfsrc_mgr = {
    .signature = UPIPE_AVFSRC_SIGNATURE,

    .upipe_alloc = upipe_avfsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_avfsrc_control,
    .upipe_free = upipe_avfsrc_free,

    .upipe_mgr_free = NULL
};

/** @This is called when the proxy is released.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsrc_proxy_released(struct upipe *upipe)
{
    upipe_avfsrc_throw_sub_subs(upipe, UPROBE_SOURCE_END);
}

/** @This returns the management structure for all avformat source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsrc_mgr_alloc(void)
{
    return upipe_proxy_mgr_alloc(&upipe_avfsrc_mgr,
                                 upipe_avfsrc_proxy_released);
}
