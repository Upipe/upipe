/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe sink module libavformat wrapper
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
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-av/upipe_avformat_sink.h>

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

/** @internal @This is the private context of an avformat source pipe. */
struct upipe_avfsink {
    /** list of subs */
    struct ulist subs;

    /** URI */
    char *uri;
    /** MIME type */
    char *mime;

    /** avformat options */
    AVDictionary *options;
    /** avformat context opened from URL */
    AVFormatContext *context;
    /** true if the header has already been written */
    bool opened;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsink, upipe, UPIPE_AVFSINK_SIGNATURE)
UPIPE_HELPER_VOID(upipe_avfsink)

/** @internal @This is the private context of an output of an avformat source
 * pipe. */
struct upipe_avfsink_sub {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** libavformat stream ID */
    int id;

    /** buffered urefs */
    struct ulist urefs;
    /** next DTS that is supposed to be dequeued */
    uint64_t next_dts;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_avfsink_sub, upipe, UPIPE_AVFSINK_INPUT_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_avfsink_sub, "block.")

UPIPE_HELPER_SUBPIPE(upipe_avfsink, upipe_avfsink_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static void upipe_avfsink_mux(struct upipe *upipe, struct upump *upump);

/** @internal @This allocates an output subpipe of an avfsink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsink_sub_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_sub_mgr(mgr);
    if (upipe_avfsink->opened)
        return NULL;

    struct uref *flow_def;
    struct upipe *upipe = upipe_avfsink_sub_alloc_flow(mgr, uprobe, signature,
                                                       args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_avfsink_sub *upipe_avfsink_sub =
        upipe_avfsink_sub_from_upipe(upipe);
    upipe_avfsink_sub->id = upipe_avfsink->context->nb_streams;

    const char *def;
    enum AVCodecID codec_id;
    if (!uref_flow_get_def(flow_def, &def) ||
        !(codec_id = upipe_av_from_flow_def(def + strlen("block."))) ||
        codec_id >= AV_CODEC_ID_FIRST_SUBTITLE) {
        uref_free(flow_def);
        upipe_avfsink_sub_free_flow(upipe);
        return NULL;
    }
    uint64_t octetrate = 0;
    uref_block_flow_get_octetrate(flow_def, &octetrate);

    struct urational fps, sar;
    uint64_t width, height;
    uint8_t channels;
    uint64_t rate, samples;
    if (codec_id < AV_CODEC_ID_FIRST_AUDIO) {
        if (unlikely(!uref_pic_flow_get_fps(flow_def, &fps) ||
                     !uref_pic_get_aspect(flow_def, &sar) ||
                     !uref_pic_get_hsize(flow_def, &width) ||
                     !uref_pic_get_vsize(flow_def, &height))) {
            uref_free(flow_def);
            upipe_avfsink_sub_free_flow(upipe);
            return NULL;
        }
    } else {
        if (unlikely(!uref_sound_flow_get_channels(flow_def, &channels) ||
                     !uref_sound_flow_get_rate(flow_def, &rate) ||
                     !uref_sound_flow_get_samples(flow_def, &samples))) {
            uref_free(flow_def);
            upipe_avfsink_sub_free_flow(upipe);
            return NULL;
        }
    }

    uint8_t *extradata_alloc = NULL;
    const uint8_t *extradata;
    size_t extradata_size = 0;
    if (uref_flow_get_headers(flow_def, &extradata, &extradata_size)) {
        extradata_alloc = malloc(extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (unlikely(extradata_alloc == NULL)) {
            uref_free(flow_def);
            upipe_avfsink_sub_free_flow(upipe);
            return NULL;
        }
        memcpy(extradata_alloc, extradata, extradata_size);
        memset(extradata_alloc + extradata_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);
    }
    uref_free(flow_def);

    upipe_avfsink_sub_init_sub(upipe);
    ulist_init(&upipe_avfsink_sub->urefs);
    upipe_avfsink_sub->next_dts = UINT64_MAX;

    upipe_use(upipe_avfsink_to_upipe(upipe_avfsink));
    upipe_throw_ready(upipe);

    AVStream *stream = avformat_new_stream(upipe_avfsink->context, NULL);
    if (unlikely(stream == NULL)) {
        upipe_err_va(upipe, "couldn't allocate stream");
        upipe_throw_fatal(upipe, UPROBE_ERR_EXTERNAL);
        return upipe;
    }

    AVCodecContext *codec = stream->codec;
    codec->bit_rate = octetrate * 8;
    codec->codec_tag =
        av_codec_get_tag(upipe_avfsink->context->oformat->codec_tag, codec_id);
    codec->codec_id = codec_id;
    if (codec_id < AV_CODEC_ID_FIRST_AUDIO) {
        codec->codec_type = AVMEDIA_TYPE_VIDEO;
        codec->width = width;
        codec->height = height;
        stream->sample_aspect_ratio.num =
            codec->sample_aspect_ratio.num = sar.num;
        stream->sample_aspect_ratio.den =
            codec->sample_aspect_ratio.den = sar.den;
        codec->time_base.num = fps.den;
        codec->time_base.den = fps.num * 2;
        codec->ticks_per_frame = 2;
    } else {
        codec->codec_type = AVMEDIA_TYPE_AUDIO;
        codec->channels = channels;
        codec->sample_rate = rate;
        codec->time_base = (AVRational){ 1, codec->sample_rate };
        codec->frame_size = samples;
    }

    if (extradata_alloc != NULL) {
        codec->extradata_size = extradata_size;
        codec->extradata = extradata_alloc;
    }

    return upipe;
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_avfsink_sub_input(struct upipe *upipe, struct uref *uref,
                                    struct upump *upump)
{
    struct upipe_avfsink_sub *upipe_avfsink_sub =
        upipe_avfsink_sub_from_upipe(upipe);

    if (unlikely(uref->ubuf == NULL)) {
        /* TODO */
        uref_free(uref);
        return;
    }

    uint64_t dts;
    if (unlikely(!uref_clock_get_dts(uref, &dts))) {
        upipe_warn_va(upipe, "packet without DTS");
        uref_free(uref);
        return;
    }

    bool was_empty = ulist_empty(&upipe_avfsink_sub->urefs);
    ulist_add(&upipe_avfsink_sub->urefs, uref_to_uchain(uref));
    if (was_empty) {
        upipe_use(upipe);
        upipe_avfsink_sub->next_dts = dts;
    }

    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    upipe_avfsink_mux(upipe_avfsink_to_upipe(upipe_avfsink), upump);
}

/** @internal @This processes control commands on an output subpipe of an
 * avfsink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_avfsink_sub_control(struct upipe *upipe,
                                      enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_avfsink_sub_get_super(upipe, p);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_sub_free(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink =
        upipe_avfsink_from_sub_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_avfsink_sub_clean_sub(upipe);
    upipe_avfsink_sub_free_flow(upipe);

    upipe_avfsink_mux(upipe_avfsink_to_upipe(upipe_avfsink), NULL);
    upipe_release(upipe_avfsink_to_upipe(upipe_avfsink));
}

/** @internal @This initializes the output manager for an avfsink pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_avfsink->sub_mgr;
    sub_mgr->signature = UPIPE_AVFSINK_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_avfsink_sub_alloc;
    sub_mgr->upipe_input = upipe_avfsink_sub_input;
    sub_mgr->upipe_control = upipe_avfsink_sub_control;
    sub_mgr->upipe_free = upipe_avfsink_sub_free;
    sub_mgr->upipe_mgr_free = NULL;
}

/** @internal @This allocates an avfsink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_avfsink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_avfsink_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    upipe_avfsink_init_sub_mgr(upipe);
    upipe_avfsink_init_sub_subs(upipe);

    upipe_avfsink->uri = NULL;
    upipe_avfsink->mime = NULL;

    upipe_avfsink->options = NULL;
    upipe_avfsink->context = NULL;
    upipe_avfsink->opened = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This finds the input with the lowest DTS.
 *
 * @param upipe description structure of the pipe
 * @return a pointer to the sub pipe, or NULL if not all inputs have packets
 */
static struct upipe_avfsink_sub *upipe_avfsink_find_input(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct uchain *uchain;
    uint64_t earliest_dts = UINT64_MAX;
    struct upipe_avfsink_sub *earliest_input = NULL;
    ulist_foreach (&upipe_avfsink->subs, uchain) {
        struct upipe_avfsink_sub *input = upipe_avfsink_sub_from_uchain(uchain);
        if (input->next_dts == UINT64_MAX)
            return NULL;
        if (input->next_dts < earliest_dts) {
            earliest_dts = input->next_dts;
            earliest_input = input;
        }
    }
    if (earliest_input != NULL) {
        uchain = ulist_peek(&earliest_input->urefs);
        if (uchain == NULL)
            return NULL; /* wait for the incoming packet */
    }
    return earliest_input;
}

/** @internal @This asks avformat to multiplex some data.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the last buffer
 */
static void upipe_avfsink_mux(struct upipe *upipe, struct upump *upump)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    struct upipe_avfsink_sub *input;
    while ((input = upipe_avfsink_find_input(upipe)) != NULL) {
        if (unlikely(!upipe_avfsink->opened)) {
            upipe_dbg(upipe, "writing header");
            if (!(upipe_avfsink->context->oformat->flags & AVFMT_NOFILE)) {
                int error = avio_open(&upipe_avfsink->context->pb,
                                      upipe_avfsink->context->filename,
                                      AVIO_FLAG_WRITE);
                if (error < 0) {
                    upipe_av_strerror(error, buf);
                    upipe_err_va(upipe, "couldn't open file %s (%s)",
                                 upipe_avfsink->context->filename, buf);
                    upipe_throw_fatal(upipe, UPROBE_ERR_EXTERNAL);
                    return;
                }
            }

            AVDictionary *options = NULL;
            av_dict_copy(&options, upipe_avfsink->options, 0);
            av_dict_free(&options);
            int error = avformat_write_header(upipe_avfsink->context, &options);
            if (unlikely(error < 0)) {
                upipe_av_strerror(error, buf);
                upipe_err_va(upipe, "couldn't write header (%s)", buf);
                upipe_throw_fatal(upipe, UPROBE_ERR_EXTERNAL);
                return;
            }
            AVDictionaryEntry *e = NULL;
            while ((e = av_dict_get(options, "", e, AV_DICT_IGNORE_SUFFIX)))
                upipe_warn_va(upipe, "unknown option \"%s\"", e->key);
            av_dict_free(&options);
            upipe_avfsink->opened = true;
        }

        AVStream *stream = upipe_avfsink->context->streams[input->id];
        struct uchain *uchain = ulist_pop(&input->urefs);
        struct uref *uref = uref_from_uchain(uchain);

        if (ulist_empty(&input->urefs)) {
            uint64_t duration;
            if (uref_clock_get_duration(uref, &duration))
                input->next_dts += duration;
            else
                input->next_dts = UINT64_MAX;
            upipe_release(upipe_avfsink_sub_to_upipe(input));
        } else {
            uchain = ulist_peek(&input->urefs);
            struct uref *next_uref = uref_from_uchain(uchain);
            uref_clock_get_dts(next_uref, &input->next_dts);
        }

        AVPacket avpkt;
        memset(&avpkt, 0, sizeof(AVPacket));
        av_init_packet(&avpkt);
        avpkt.stream_index = input->id;
        if (uref_flow_get_random(uref))
            avpkt.flags |= AV_PKT_FLAG_KEY;

        uint64_t dts;
        if (uref_clock_get_dts(uref, &dts))
            avpkt.dts = dts * stream->time_base.den / UCLOCK_FREQ /
                        stream->time_base.num;
        uint64_t pts;
        if (uref_clock_get_pts(uref, &pts))
            avpkt.pts = pts * stream->time_base.den / UCLOCK_FREQ /
                        stream->time_base.num;

        size_t size = 0;
        uref_block_size(uref, &size);
        if (unlikely(!size)) {
            upipe_warn(upipe, "Received packet with size 0, dropping");
            uref_free(uref);
            continue;
        }
        avpkt.size = size;

        /* TODO replace with umem */
        avpkt.data = malloc(avpkt.size);
        if (unlikely(avpkt.data == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }
        uref_block_extract(uref, 0, avpkt.size, avpkt.data); 
        uref_free(uref);

        int error = av_write_frame(upipe_avfsink->context, &avpkt);
        free(avpkt.data);
        if (unlikely(error < 0)) {
            upipe_av_strerror(error, buf);
            upipe_err_va(upipe, "write error to %s (%s)", upipe_avfsink->uri, buf);
            upipe_throw_sink_end(upipe);
            return;
        }
    }
}

/** @internal @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return false in case of error
 */
static bool _upipe_avfsink_get_option(struct upipe *upipe, const char *option,
                                      const char **content_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(option != NULL);
    assert(content_p != NULL);
    AVDictionaryEntry *entry = av_dict_get(upipe_avfsink->options, option,
                                           NULL, 0);
    if (unlikely(entry == NULL))
        return false;
    *content_p = entry->value;
    return true;
}

/** @internal @This sets the content of an avformat option. It only take effect
 * after the next call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static bool _upipe_avfsink_set_option(struct upipe *upipe, const char *option,
                                     const char *content)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(option != NULL);
    int error = av_dict_set(&upipe_avfsink->options, option, content, 0);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't set option %s:%s (%s)", option, content,
                     buf);
        return false;
    }
    return true;
}

/** @internal @This returns the currently configured MIME type.
 *
 * @param upipe description structure of the pipe
 * @param mime_p filled in with the currently configured MIME type
 * @return false in case of error
 */
static bool _upipe_avfsink_get_mime(struct upipe *upipe, const char **mime_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(mime_p != NULL);
    *mime_p = upipe_avfsink->mime;
    return true;
}

/** @internal @This sets the MIME type. It only takes effect after the next
 * call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param mime MIME type
 * @return false in case of error
 */
static bool _upipe_avfsink_set_mime(struct upipe *upipe, const char *mime)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    free(upipe_avfsink->mime);
    if (mime != NULL) {
        upipe_avfsink->mime = strdup(mime);
        if (upipe_avfsink->mime == NULL) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return false;
        }
    } else
        upipe_avfsink->mime = NULL;
    return true;
}

/** @internal @This returns the currently opened URI.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the URI
 * @return false in case of error
 */
static bool upipe_avfsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_avfsink->uri;
    return true;
}

/** @internal @This asks to open the given URI.
 *
 * @param upipe description structure of the pipe
 * @param uri URI to open
 * @return false in case of error
 */
static bool upipe_avfsink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);

    if (unlikely(upipe_avfsink->context != NULL)) {
        if (likely(upipe_avfsink->uri != NULL))
            upipe_notice_va(upipe, "closing URI %s", upipe_avfsink->uri);
        if (upipe_avfsink->opened) {
            upipe_dbg(upipe, "writing trailer");
            av_write_trailer(upipe_avfsink->context);
            if (!(upipe_avfsink->context->oformat->flags & AVFMT_NOFILE))
                avio_close(upipe_avfsink->context->pb);
        }
        avformat_free_context(upipe_avfsink->context);
    }
    free(upipe_avfsink->uri);
    upipe_avfsink->uri = NULL;

    if (unlikely(uri == NULL))
        return true;

    AVOutputFormat *format = NULL;
    if (upipe_avfsink->mime != NULL)
        format = av_guess_format(NULL, NULL, upipe_avfsink->mime);
    if (format == NULL)
        format = av_guess_format(NULL, uri, NULL);
    if (unlikely(format == NULL))
        return false;

    int error = avformat_alloc_output_context2(&upipe_avfsink->context,
                                               format, NULL, uri);
    if (unlikely(error < 0)) {
        upipe_av_strerror(error, buf);
        upipe_err_va(upipe, "can't open URI %s (%s)", uri, buf);
        return false;
    }

    upipe_avfsink->uri = strdup(uri);
    upipe_avfsink->opened = false;
    upipe_notice_va(upipe, "opening URI %s", upipe_avfsink->uri);
    return true;
}

/** @internal @This processes control commands on an avformat source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_avfsink_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_avfsink_get_sub_mgr(upipe, p);
        }
        case UPIPE_AVFSINK_GET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSINK_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char **content_p = va_arg(args, const char **);
            return _upipe_avfsink_get_option(upipe, option, content_p);
        }
        case UPIPE_AVFSINK_SET_OPTION: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSINK_SIGNATURE);
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return _upipe_avfsink_set_option(upipe, option, content);
        }
        case UPIPE_AVFSINK_GET_MIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSINK_SIGNATURE);
            const char **mime_p = va_arg(args, const char **);
            return _upipe_avfsink_get_mime(upipe, mime_p);
        }
        case UPIPE_AVFSINK_SET_MIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_AVFSINK_SIGNATURE);
            const char *mime = va_arg(args, const char *);
            return _upipe_avfsink_set_mime(upipe, mime);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_avfsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_avfsink_set_uri(upipe, uri);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_avfsink_free(struct upipe *upipe)
{
    struct upipe_avfsink *upipe_avfsink = upipe_avfsink_from_upipe(upipe);
    upipe_avfsink_clean_sub_subs(upipe);

    upipe_avfsink_set_uri(upipe, NULL);
    upipe_throw_dead(upipe);

    av_dict_free(&upipe_avfsink->options);

    upipe_avfsink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_avfsink_mgr = {
    .signature = UPIPE_AVFSINK_SIGNATURE,

    .upipe_alloc = upipe_avfsink_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_avfsink_control,
    .upipe_free = upipe_avfsink_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all avformat sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsink_mgr_alloc(void)
{
    return &upipe_avfsink_mgr;
}
